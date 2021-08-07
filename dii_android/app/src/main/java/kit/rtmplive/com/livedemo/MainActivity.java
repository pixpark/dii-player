package kit.rtmplive.com.livedemo;

import android.content.Intent;
import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;
import android.view.View;
import android.widget.EditText;

import org.dii.core.DiiMediaCore;

public class MainActivity extends AppCompatActivity {
    private EditText fileNameInput;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        DiiMediaCore.Inst();
    }

    public void OnBtnClicked(View view) {

        if (view.getId() == R.id.btn_start_live) {
//            Intent it = new Intent(this, DiiPublishActivity.class);
//            startActivity(it);
        } else {
            Intent it = new Intent(this, DiiPlayerActivity.class);
            startActivity(it);
        }
    }
}
