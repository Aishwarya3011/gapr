package goulf.gapr;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Parcelable;
import android.os.SystemClock;
import android.util.Log;
import android.util.SparseArray;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.annotation.Nullable;

import java.util.LinkedList;

public class Main extends Activity {

    private String fatal_error = null;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.v("gapr", "---------main onCreate");

        if (savedInstanceState != null) {
            fatal_error = savedInstanceState.getString("fatal_error");
            if (fatal_error != null) {
                displayError(fatal_error);
                return;
            }
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && false) {
            String[] perms = new String[1];
            perms[0] = Manifest.permission.READ_CONTACTS;
            requestPermissions(perms, 99);
        }

        setContentView(R.layout.login_page);
        boolean restored = false;
        if (savedInstanceState != null) {
            SparseArray<Parcelable> container = savedInstanceState.getSparseParcelableArray("login_page_state");
            if (container != null) {
                View view = findViewById(R.id.login_page);
                view.restoreHierarchyState(container);
                restored = true;
            }
        }
        if (!restored) {
            SharedPreferences pref = getSharedPreferences("login", MODE_PRIVATE);
            String u = pref.getString("u", null);
            String p = pref.getString("p", null);
            String r = pref.getString("r", null);
            if (u != null)
                ((EditText) findViewById(R.id.username)).setText(u);
            if (p != null)
                ((EditText) findViewById(R.id.password)).setText(p);
            if (r != null)
                ((EditText) findViewById(R.id.repo)).setText(r);
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        Log.v("gapr", "---------main onDestroy");
    }

    @Override
    protected void onStart() {
        super.onStart();
        Log.v("gapr", "---------main onStart");
    }

    @Override
    protected void onStop() {
        super.onStop();
        Log.v("gapr", "---------main onStop");
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.v("gapr", "---------main onResume");
    }

    @Override
    protected void onPause() {
        super.onPause();
        Log.v("gapr", "---------main onPause");
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        Log.v("gapr", "---------main onSaveInstanceState");
        if (fatal_error == null) {
            SparseArray<Parcelable> container = new SparseArray<>();
            View view = findViewById(R.id.login_page);
            view.saveHierarchyState(container);
            outState.putSparseParcelableArray("login_page_state", container);
        } else {
            outState.putString("fatal_error", fatal_error);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        Log.v("gapr", "---------main onWindowFocusChanged");
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        Log.v("gapr", "---------main onConfigurationChanged");
    }

    public void onLoginClick(android.view.View view) {
        String u = ((EditText) findViewById(R.id.username)).getText().toString();
        String p = ((EditText) findViewById(R.id.password)).getText().toString();
        String r = ((EditText) findViewById(R.id.repo)).getText().toString();

        view.setEnabled(false);
        ((TextView) findViewById(R.id.error_msg)).setText(null);
        ((ProgressBar) findViewById(R.id.progr_open)).setVisibility(View.VISIBLE);

        Intent intent = new Intent(this, Proofread.class);
        intent.setData(Uri.fromParts("gapr", r, ""));
        intent.putExtra("u", u);
        intent.putExtra("p", p);
        startActivityForResult(intent, 33, null);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        switch (requestCode) {
            case 33:
                Log.v("gapr", "result code " + resultCode);
                switch (resultCode) {
                    case -1:
                        ((ProgressBar) findViewById(R.id.progr_open)).setVisibility(View.GONE);
                        ((TextView) findViewById(R.id.error_msg)).setText(data.getStringExtra("err"));
                        ((Button) findViewById(R.id.login)).setEnabled(true);
                        // XXX set focus if input err
                    case 0:
                        ((ProgressBar) findViewById(R.id.progr_open)).setVisibility(View.GONE);
                        ((Button) findViewById(R.id.login)).setEnabled(true);
                        break;
                    case -2:
                        fatal_error = data.getStringExtra("err");
                        displayError(fatal_error);
                        break;
                }
                break;
        }
    }

    private LinkedList<Long> icon_clicks = new LinkedList<Long>();

    public void onIconClick(android.view.View view) {
        long t = SystemClock.uptimeMillis();
        if (icon_clicks.size() < 7) {
            icon_clicks.push(t);
        } else if (t - icon_clicks.getLast() < 5000) {
            icon_clicks.clear();
            fatal_error = "triggered error";
            displayError(fatal_error);
        } else {
            icon_clicks.push(t);
            icon_clicks.removeLast();
        }
    }

    public void onQuitClick(android.view.View view) {
        finishAfterTransition();
    }

    public void onLinkClick(android.view.View view) {
        //startActivity(new Intent(URL))
    }

    void displayError(String msg) {
        setContentView(R.layout.error_page);
        TextView tv = findViewById(R.id.error_msg);
        tv.setText(msg);
        TextView info = (TextView) findViewById(R.id.sys_info);
        if (info != null) {
            Configuration config = getResources().getConfiguration();
            String tt = config.toString();
            info.setText(/*get_infos()+*/tt);
            info.setVisibility(View.VISIBLE);
        }
    }

}

