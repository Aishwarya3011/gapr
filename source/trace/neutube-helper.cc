/* This code is based on
 *   bwsolid.c  imbinarize.c  trace_seed.c ...
 * from neuTube.
 */

#include <array>
#include <cassert>
#include <cmath>
#include <vector>

extern "C" {
#include "image_lib.h"
#include "tz_darray.h"
#include "tz_int_histogram.h"
#include "tz_locseg_chain.h"
#include "tz_math.h"
#include "tz_objdetect.h"
#include "tz_stack.h"
#include "tz_stack_bwmorph.h"
#include "tz_stack_lib.h"
#include "tz_stack_math.h"
#include "tz_stack_objlabel.h"
#include "tz_stack_sampling.h"
#include "tz_stack_stat.h"
#include "tz_stack_threshold.h"
#include "tz_stack_utils.h"
#include "tz_voxel_graphics.h"
#include "tz_voxel_linked_list.h"
}

static void PRINT_EXCEPTION(const char *a, const char *b) {
  printf("%s %s\n", a, b);
}

static Stack *imbinarize(Stack *refstack, int nretry, int rsobj = 0) {
  Stack *stack = refstack;

  int thre = 0;
  int *hist = NULL;

  printf("Thresholding %s ...\n", "");

  Stack *locmax = NULL;
  int low, high;

  {
    {
      if(stack == refstack) {
        stack = Copy_Stack(refstack);
      }

#if 0
			locmax = Stack_Local_Max(refstack, NULL, STACK_LOCMAX_SINGLE);
#else
      int conn = 18;
      locmax = Stack_Locmax_Region(refstack, conn);
      Stack_Label_Objects_Ns(locmax, NULL, 1, 2, 3, conn);
      int nvoxel = Stack_Voxel_Number(locmax);
      int i;

      for(i = 0; i < nvoxel; i++) {
        if(locmax->array[i] < 3) {
          locmax->array[i] = 0;
        } else {
          locmax->array[i] = 1;
        }
      }
#endif
      hist = Stack_Hist_M(refstack, locmax);

#ifdef _DEBUG_
      printf("%d %d\n", hist[0], hist[1]);
      iarray_write("../data/test.bn", hist, Int_Histogram_Length(hist) + 2);
#endif

      Kill_Stack(locmax);

      Int_Histogram_Range(hist, &low, &high);

      thre = Int_Histogram_Triangle_Threshold(hist, low, high - 1);
      printf("Threshold: %d\n", thre);
      // Stack_Threshold(stack, thre);
    }
  }

  double fgratio =
      (double)Stack_Fgarea_T(stack, thre) / Stack_Voxel_Number(stack);
  printf("Foreground: %g\n", fgratio);

  int succ = 1;

  double ratio_low_thre = 0.01;
  double ratio_thre = 0.05;
  int thre2;
  double fgratio2 = fgratio;
  double prev_fgratio = fgratio;

  {
    if((fgratio > ratio_low_thre) && (fgratio <= ratio_thre)) {
      thre2 = Int_Histogram_Triangle_Threshold(hist, thre + 1, high - 1);
      fgratio2 =
          (double)Stack_Fgarea_T(stack, thre2) / Stack_Voxel_Number(stack);
      printf("Threshold: %d\n", thre2);
      printf("Foreground: %g\n", fgratio2);
      if(fgratio2 / fgratio <= 0.3) {
        thre = thre2;
      }
    } else {
      thre2 = thre;
      while(fgratio2 > ratio_thre) {
        // ASSERT(locmax != NULL, "bug found");
        printf("Bad threshold, retrying ...\n");

        thre2 = Int_Histogram_Triangle_Threshold(hist, thre2 + 1, high - 1);
        printf("Threshold: %d\n", thre2);

        // stack = Read_Stack(image_file);
        // Stack_Threshold(stack, thre);
        fgratio2 =
            (double)Stack_Fgarea_T(stack, thre2) / Stack_Voxel_Number(stack);
        if(fgratio2 / prev_fgratio <= 0.5) {
          thre = thre2;
        }
        prev_fgratio = fgratio2;

        printf("Foreground: %g\n", fgratio2);

        nretry--;

        if(nretry == 0) {
          break;
        }
      }

      if(fgratio2 > ratio_thre * 4.0) {
        succ = 0;
      } /* else if (fgratio2 / fgratio <= 0.5) {
           thre = thre2;
           }  */
    }
  }

  if(succ == 0) {
    PRINT_EXCEPTION("Thresholding error", "The threshold seems wrong.");
    printf("Try it anyway.\n");
  }
  {
    succ = 1;
    printf("Binarizing with threshold %d ...\n", thre);
    Stack_Threshold(stack, thre);
    Stack_Binarize(stack);

    if(stack->kind != GREY) {
      Translate_Stack(stack, GREY, 1);
    }

    if(rsobj != 0) {
      auto stk_copy = Copy_Stack(stack);
      stack = Stack_Remove_Small_Object(stk_copy, stack, rsobj, 26);
      Kill_Stack(stk_copy);
    }

    if(stack->kind != GREY) {
      Translate_Stack(stack, GREY, 1);
    }
  }

  if(hist != NULL) {
    free(hist);
  }

  return stack;
}

static Stack *bwsolid(Stack *stack) {
  Stack *clear_stack = NULL;
  {
    printf("Majority filtering ...\n");
    int mnbr = 4;
    clear_stack = Stack_Majority_Filter_R(stack, NULL, 26, mnbr);
  }

  printf("Dilating ...\n");
  Struct_Element *se = Make_Cuboid_Se(3, 3, 3);
  Stack *dilate_stack = Stack_Dilate(clear_stack, NULL, se);
  Kill_Stack(clear_stack);

  /*
     printf("Hole filling ...\n");
     Stack *fill_stack = Stack_Fill_Hole_N(dilate_stack, NULL, 1, 4, NULL);
     Kill_Stack(dilate_stack);
     */
  Stack *fill_stack = dilate_stack;

  printf("Eroding ...\n");
  Stack *mask = Stack_Erode_Fast(fill_stack, NULL, se);
  Kill_Stack(fill_stack);
  Kill_Struct_Element(se);
  return mask;
}

static void rmsobj(Stack *stack, int argm) {
  int n_nbr = 26;
  Stack_Label_Large_Objects_N(stack, NULL, 1, 2, argm + 1, n_nbr);
  Stack_Threshold_Binarize(stack, 2);
}

static auto *trace_seed(Stack *mask) {
  printf("Creating seeds for tracing ...\n");

  Translate_Stack(mask, GREY, 1);

  printf("  Building distance map ...\n");
  Stack *dist = Stack_Bwdist_L_U16(mask, NULL, 0);

  Stack *seeds = NULL;

  Stack *distw = dist;
  if(seeds == NULL) {

    {
      // seeds = Stack_Local_Max(dist, NULL, STACK_LOCMAX_FLAT);
      // Stack_Clean_Locmax(dist, seeds);
      seeds = Stack_Locmax_Region(distw, 26);

      Object_3d_List *objs = Stack_Find_Object_N(seeds, NULL, 1, 0, 26);
      auto objs_del = objs;
      Zero_Stack(seeds);
      int objnum = 0;
      while(objs != NULL) {
        Object_3d *obj = objs->data;
        Voxel_t center;
        Object_3d_Central_Voxel(obj, center);
        Set_Stack_Pixel(seeds, center[0], center[1], center[2], 0, 1);

        {
          int erase_size = iround(Get_Stack_Pixel(dist, center[0], center[1],
                                                  center[2], 0)) *
                           2;
          int u, v, w;
          for(w = -erase_size; w <= erase_size; w++) {
            for(v = -erase_size; v <= erase_size; v++) {
              for(u = -erase_size; u <= erase_size; u++) {
                int sub[3];
                sub[0] = center[0] + u;
                sub[1] = center[1] + v;
                sub[2] = center[2] + w;
                if(IS_IN_OPEN_RANGE3(sub[0], sub[1], sub[2], -1, seeds->width,
                                     -1, seeds->height, -1, seeds->depth)) {
                  Set_Stack_Pixel(distw, sub[0], sub[1], sub[2], 0, 0.0);
                }
              }
            }
          }
        }

        objs = objs->next;
        objnum++;
      }
      Kill_Object_3d_List(objs_del);

      {
        Stack *seed_stack2 = Stack_Locmax_Region(distw, 26);
        objs_del = objs = Stack_Find_Object_N(seed_stack2, NULL, 1, 0, 26);
        while(objs != NULL) {
          Object_3d *obj = objs->data;
          Voxel_t center;
          Object_3d_Central_Voxel(obj, center);
          Set_Stack_Pixel(seeds, center[0], center[1], center[2], 0, 1);
          objs = objs->next;
        }
        Kill_Object_3d_List(objs_del);
        Kill_Stack(seed_stack2);
      }
    }
  }

  Voxel_List *list = Stack_To_Voxel_List(seeds);
  Pixel_Array *pa = Voxel_List_Sampling(dist, list);
  Voxel_P *voxel_array = Voxel_List_To_Array(list, 1, NULL, NULL);
  // double *pa_array = (double *) pa->array;
  uint16 *pa_array = (uint16 *)pa->array;

  printf("%d seeds found.\n", pa->size);

  Geo3d_Scalar_Field *field = Make_Geo3d_Scalar_Field(pa->size);
  field->size = 0;
  int i;
  for(i = 0; i < pa->size; i++) {
    if(IS_IN_OPEN_RANGE3(voxel_array[i]->x, voxel_array[i]->y,
                         voxel_array[i]->z, 0, seeds->width - 1, 0,
                         seeds->height - 1, 0, seeds->depth - 1)) {
      field->points[field->size][0] = voxel_array[i]->x;
      field->points[field->size][1] = voxel_array[i]->y;
      field->points[field->size][2] = voxel_array[i]->z;
      field->values[field->size] = sqrt((double)pa_array[i]);
      field->size++;
    }
  }

  Kill_Stack(dist);

  /*
     printf("mean: %g, std: %g\n", gsl_stats_mean(field->values, 1, pa->size),
     sqrt(gsl_stats_variance(field->values, 1, pa->size)));
     printf("max: %g\n", gsl_stats_max(field->values, 1, pa->size));
     */

  free(voxel_array);
  Kill_Stack(seeds);
  Kill_Voxel_List(list);
  Kill_Pixel_Array(pa);

  return field;
}

static auto *sort_seed(Stack *signal, Geo3d_Scalar_Field *seed,
                       double z_scale) {
  size_t idx;
  double max_r = darray_max(seed->values, seed->size, &idx);

  max_r *= 1.5;

  // Set_Neuroseg_Max_Radius(max_r);

  dim_type dim[3];
  dim[0] = signal->width;
  dim[1] = signal->height;
  dim[2] = signal->depth;

  Rgb_Color color;
  Set_Color(&color, 255, 0, 0);

  int seed_offset = -1;

  printf("z scale: %g\n", z_scale);

  tic();

  double *values = darray_malloc(seed->size);

  int i;
  Local_Neuroseg *locseg =
      (Local_Neuroseg *)malloc(seed->size * sizeof(Local_Neuroseg));

  int index = 0;

  // int ncol = LOCAL_NEUROSEG_NPARAM + 1 + 23;
  // double *features = darray_malloc(seed->size * ncol);
  // double *tmpfeats = features;

  Stack *seed_mask =
      Make_Stack(GREY, signal->width, signal->height, signal->depth);
  Zero_Stack(seed_mask);

  Locseg_Fit_Workspace *fws = New_Locseg_Fit_Workspace();

  fws->sws->fs.n = 2;
  fws->sws->fs.options[0] = STACK_FIT_DOT;
  fws->sws->fs.options[1] = STACK_FIT_CORRCOEF;

  for(i = 0; i < seed->size; i++) {
    printf("-----------------------------> seed: %d / %d\n", i, seed->size);

    index = i;
    int x = (int)seed->points[index][0];
    int y = (int)seed->points[index][1];
    int z = (int)seed->points[index][2];

    double width = seed->values[index];

    seed_offset = Stack_Util_Offset(x, y, z, signal->width, signal->height,
                                    signal->depth);

    if(width < 3.0) {
      width += 0.5;
    }
    Set_Neuroseg(&(locseg[i].seg), width, 0.0, NEUROSEG_DEFAULT_H, 0.0, 0.0,
                 0.0, 0.0, 1.0);

    double cpos[3];
    cpos[0] = x;
    cpos[1] = y;
    cpos[2] = z;
    cpos[2] /= z_scale;

    Set_Neuroseg_Position(&(locseg[i]), cpos, NEUROSEG_CENTER);

    if(seed_mask->array[seed_offset] > 0) {
      printf("labeled\n");
      values[i] = 0.0;
      continue;
    }

    // Local_Neuroseg_Optimize(locseg + i, signal, z_scale, 0);
    Local_Neuroseg_Optimize_W(locseg + i, signal, z_scale, 0, fws);

    values[i] = fws->sws->fs.scores[1];
    /*
       Stack_Fit_Score fs;
       fs.n = 1;
       fs.options[0] = 1;
       values[i] = Local_Neuroseg_Score(locseg + i, signal, z_scale, &fs);
       */

    // values[i] = Local_Neuroseg_Score_W(locseg + i, signal, z_scale, sws);

    printf("%g\n", values[i]);

    double min_score = LOCAL_NEUROSEG_MIN_CORRCOEF;
    { min_score = 0.35; }

    if(values[i] > min_score) {
      Local_Neuroseg_Label_G(locseg + i, seed_mask, -1, 2, z_scale);
    } else {
      Local_Neuroseg_Label_G(locseg + i, seed_mask, -1, 1, z_scale);
    }
  }
  free(values);
  Kill_Stack(seed_mask);
  Kill_Locseg_Fit_Workspace(fws);
  return locseg;
}
auto *extract_line(Stack *stack) {

  printf("Extracting line structure ...\n");

  double sigma[] = {1.0, 1.0, 1.0};

  FMatrix *result = NULL;

  tic();
  if(stack->width * stack->height * stack->depth > 1024 * 1024 * 100) {
    result = El_Stack_L_F(stack, sigma, NULL);
  } else {
    result = El_Stack_F(stack, sigma, NULL);
  }

  Stack *out = Scale_Float_Stack(result->array, result->dim[0], result->dim[1],
                                 result->dim[2], GREY16);

  Kill_FMatrix(result);

  return out;
}

void calc_or(Stack *img1, Stack *img2) {
  auto dispatch = [](Stack *stk, auto func) {
    Image_Array img;
    img.array = stk->array;
    switch(stk->kind) {
    case GREY:
      func(img.array);
      break;
    case GREY16:
      func(img.array16);
      break;
    case FLOAT32:
      func(img.array32);
      break;
    case FLOAT64:
      func(img.array64);
      break;
    default:
      assert(0);
    }
  };
  dispatch(img1, [dispatch, img2](auto *p1) {
    dispatch(img2, [img2, p1](auto *p2) {
      auto n = Stack_Voxel_Number(img2);
      for(std::size_t offset = 0; offset < n; offset++) {
        using tp = std::decay_t<decltype(p1[0] + p2[0])>;
        using ot = std::decay_t<decltype(p1[0])>;
        p1[offset] = std::min<tp>(std::max<tp>(p1[offset], p2[offset]),
                                  std::numeric_limits<ot>::max());
      }
    });
  });
};

std::vector<std::array<double, 4>> find_seeds_neutube(Stack *stack,
                                                      double z_scale) {
  std::vector<std::array<double, 4>> seedsout;
  auto bin = imbinarize(stack, 3);
  auto mask = bwsolid(bin);
  Kill_Stack(bin);
  rmsobj(mask, 10);
  auto grey_line = extract_line(stack);
  auto line = imbinarize(grey_line, 5, 27);
  calc_or(mask, line);
  Kill_Stack(grey_line);
  Kill_Stack(line);
  auto seeds = trace_seed(mask);
  Kill_Stack(mask);
  auto chain_seeds = sort_seed(stack, seeds, z_scale);
  for(int i = 0; i < seeds->size; ++i) {
    auto &r = seedsout.emplace_back();
    for(unsigned int k = 0; k < 3; ++k)
      r[k] = chain_seeds[i].pos[k];
    r[2] *= z_scale;
    r[3] = 0.0;
  }
  Kill_Geo3d_Scalar_Field(seeds);
  free(chain_seeds);
  return seedsout;
}

auto voxelNumber(const Stack *stk) {
  return long(stk->width) * stk->height * stk->depth;
};

static int *calc_hist(const Stack *stack) {
  if(voxelNumber(stack) > MAX_INT32) {
    double ratio = (double)voxelNumber(stack) / MAX_INT32;
    int intv[3] = {0, 0, 0};
    int i = 0;
    while((intv[0] + 1) * (intv[1] + 1) * (intv[2] + 1) < ratio) {
      intv[i++] += 1;
      if(i > 2) {
        i = 0;
      }
    }

    Stack *ds = Downsample_Stack(stack, intv[0], intv[1], intv[2]);
    int *hist = Stack_Hist(ds);

    Kill_Stack(ds);
    return hist;
  }

  return Stack_Hist(stack);
}

static int getCount(const int *hist, int v) {
  int count = 0;
  if(hist) {
    if(v >= Int_Histogram_Min(hist) && v <= Int_Histogram_Max(hist)) {
      count = hist[v + 2 - hist[1]];
    }
  }
  return count;
}
static int getUpperCount(const int *hist, int v) {
  int count = 0;
  if(hist) {
    int minV = std::max(v, Int_Histogram_Min(hist));
    int maxV = Int_Histogram_Max(hist);
    for(int i = minV; i <= maxV; ++i) {
      count += getCount(hist, i);
    }
  }

  return count;
}
static int getMode(const int *hist, int minV, int maxV) {
  minV = std::max(minV, Int_Histogram_Min(hist));
  maxV = std::min(maxV, Int_Histogram_Max(hist));
  int m = minV;
  int maxCount = getCount(hist, m);
  for(int i = minV; i <= maxV; ++i) {
    int c = getCount(hist, i);
    if(c > maxCount) {
      maxCount = c;
      m = i;
    }
  }
  return m;
}

static void SubtractBackground(Stack *stackData, double minFr, int maxIter) {
  auto hist = calc_hist(stackData);
  int maxV = Int_Histogram_Max(hist);
  int totalCount = getUpperCount(hist, 0);
  int commonIntensity = 0;

  int darkCount = getCount(hist, 0);
  if((double)darkCount / totalCount > 0.9) {
    commonIntensity = Int_Histogram_Min(hist);
  } else {
    for(int iter = 0; iter < maxIter; ++iter) {
      int mode = getMode(hist, commonIntensity + 1, maxV);
      if(mode == maxV) {
        break;
      }

      commonIntensity = mode;
      double fgRatio =
          (double)getUpperCount(hist, commonIntensity + 1) / totalCount;
      if(fgRatio < minFr) {
        break;
      }
    }
  }

  if(commonIntensity > 0) {
    Stack_Subc(stackData, commonIntensity);
  }

  delete hist;
}

void process_stack_neutube(Stack *stack) { SubtractBackground(stack, 0.5, 3); }
