package goulf.gapr;

import android.app.NativeActivity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import static java.lang.Integer.parseInt;


public class Proofread extends android.app.NativeActivity {

    static {
        System.loadLibrary("GaprAndroid");
        prepare_library();
    }

    static int hide_flags=View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN;

    private View overlay = null;
    private WindowManager wm = null;

    public native String stringFromJNI();

    public native String open_repository(String username, String password, String repo);

    public native void update_rects(int[] data);

    public native String get_infos();

    static public native void prepare_library();

    private class Panel extends FrameLayout {
        private Proofread mAct;

        public Panel(Context context) {
            super(context);
            mAct = (Proofread) context;
        }
		  /*
        @Override public boolean dispatchKeyEvent(KeyEvent event) {
            if(mAct.dispatchKeyEvent(event))
                return true;
            return super.dispatchKeyEvent(event);
        }
		  */
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Intent intent = getIntent();
        Log.v("gapr", "intent " + intent.toString());
        String r = intent.getData().getSchemeSpecificPart();
        String u = intent.getStringExtra("u");
        String p = intent.getStringExtra("p");

        String e = open_repository(u, p, r);
        if (e != null && !e.isEmpty()) {
            Intent res = new Intent();
            res.putExtra("err", e);
            setResult(-1, res);
            finish();//AfterTransition();
            return;
        }
        SharedPreferences pref = getSharedPreferences("login", MODE_PRIVATE);
        SharedPreferences.Editor prefedit = pref.edit();
        prefedit.putString("u", u);
        prefedit.putString("p", p);
        prefedit.putString("r", r);
        prefedit.apply();

        wm = (WindowManager) getSystemService(NativeActivity.WINDOW_SERVICE);
        //displayOverlay(arg);
//////////////////////////////////////////////////////

       /*
        if(savedInstanceState!=null) {
            SparseArray<Parcelable> container = savedInstanceState.getSparseParcelableArray("current_view");
            if(container!=null) {
                Log.v("gapr", "has current_view: "+container.toString());
                int resId=savedInstanceState.getInt("current_view_res");
                displayView2(resId, true);
                curView.restoreHierarchyState(container);
            }
        }*/
        overlay = getLayoutInflater().inflate(R.layout.overlay, new Panel(this));
        wm.addView(overlay, new WindowManager.LayoutParams(WindowManager.LayoutParams.TYPE_APPLICATION, WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE, PixelFormat.RGBA_8888));
		  /*
        if(resId==R.layout.overlay) {
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        } else {
//            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
        }
		  */

        final int flags = hide_flags;
        final View decorView = getWindow().getDecorView();
        final NativeActivity act=this;
        decorView.setOnSystemUiVisibilityChangeListener(new View.OnSystemUiVisibilityChangeListener() {
            @Override
            public void onSystemUiVisibilityChange(int visibility) {
                if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
                    decorView.setSystemUiVisibility(flags);
                    act.onGlobalLayout();
                }
            }
        });
    }

    @Override
    public void onGlobalLayout() {
        super.onGlobalLayout();
        set_fullscreen(overlay);
        int[] mapping = new int[]{
                R.id.rotate, 101,
                R.id.zoom, 102,
                R.id.data_only, 103,
                R.id.xfunc, 104,
                R.id.skip_misc, 105,
                R.id.proofread_end, 106,
                R.id.extend_branch, 107,
                R.id.jump_and_report, 108,
                R.id.xfunc2, 109,
                R.id.cursor, 201,
        };
        Rect rct = new Rect();
        int[] data = new int[mapping.length / 2 * 5];
        for (int i = 0; i + 1 < mapping.length; i += 2) {
            View rot = overlay.findViewById(mapping[i]);
            if (rot == null) {
                data[i] = data[i + 1] = data[i + 2] = 0;
                continue;
            }
            rot.getHitRect(rct);
            int j = i / 2 * 5;
            data[j] = mapping[i + 1];
            data[j + 1] = rct.left;
            data[j + 2] = rct.top;
            data[j + 3] = rct.right;
            data[j + 4] = rct.bottom;
        }
        update_rects(data);

    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
    }

    @Override
    protected void onDestroy() {
        Log.v("gapr", "onDestroy");
        if (overlay != null) {
            wm.removeViewImmediate(overlay);
        }
        super.onDestroy();
    }

    @Override
    protected void onStart() {
        super.onStart();
    }

    @Override
    protected void onResume() {
        super.onResume();
        set_fullscreen(getWindow().getDecorView());
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if(hasFocus)
            set_fullscreen(getWindow().getDecorView());
    }

    @Override
    public void onBackPressed() {
        finishAfterTransition();
        //XXX press again to end
        //finishActivity();
    }

    private void set_fullscreen(View view) {
        //View view=getWindow().getDecorView();
        view.setSystemUiVisibility(hide_flags);
    }

    static void set_progress(ProgressBar p, int v) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            p.setProgress(v, true);
        } else {
            p.setProgress(v);
        }
    }

    int call_java(int action, String arg) {
        switch (action) {
            case -2: {
                Intent res = new Intent();
                res.putExtra("err", arg);
                setResult(-2, res);
                finishAfterTransition();
            }
            break;
            case -1: {
                Intent res = new Intent();
                res.putExtra("err", arg);
                setResult(-1, res);
                finishAfterTransition();
            }


            break;
            case 7:
                return isFinishing() ? 1 : 0;
            case 8:
                ((TextView) overlay.findViewById(R.id.repo_title)).setText(arg);
                break;
            case 20: {
                ProgressBar p = (ProgressBar) overlay.findViewById(R.id.progr_open);
                p.setVisibility(View.VISIBLE);
            }
            break;
            case 21: {
                ProgressBar p = (ProgressBar) overlay.findViewById(R.id.model_prog);
                p.setVisibility(View.VISIBLE);
            }
            break;
            case 22: {
                ProgressBar p = (ProgressBar) overlay.findViewById(R.id.model_prog);
                p.setVisibility(View.GONE);
            }
            break;
            case 100: {
                int v = Integer.parseInt(arg);
                ProgressBar p = (ProgressBar) overlay.findViewById(R.id.img_progr);
                if (v == -1) {
                    p.setIndeterminate(true);
                } else {
                    p.setIndeterminate(false);
                    set_progress(p, v);
                }
                p.setMax(1000);
                p.setVisibility(View.VISIBLE);
            }
            break;
            case 101: {
                int v = Integer.parseInt(arg);
                ProgressBar p = (ProgressBar) overlay.findViewById(R.id.img_progr);
                if (v == 1001) {
                    p.setVisibility(View.GONE);
                } else {
                    set_progress(p, v);
                }
            }
            break;
            case 120: {
                TextView p = (TextView) overlay.findViewById(R.id.xfunc_val1);
                if (p != null)
                    p.setText(arg);
            }
            break;
            case 121: {
                TextView p = (TextView) overlay.findViewById(R.id.xfunc_val0);
                if (p != null)
                    p.setText(arg);
            }
            break;

        }
        return 0;
    }

}

