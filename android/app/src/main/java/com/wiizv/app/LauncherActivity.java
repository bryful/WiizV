package com.wiizv.app;

import android.app.*;
import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.graphics.Color;
import android.view.View;
import android.widget.*;
import java.io.*;
import java.util.*;

public final class LauncherActivity extends Activity {
    private static final int FOLDER=10, ROM=11;
    private DocumentStore store;
    private TextView status;
    private boolean busy;
    private android.content.SharedPreferences prefs;
    @Override public void onCreate(Bundle state){
        super.onCreate(state);prefs=getSharedPreferences("startup",MODE_PRIVATE);
        LinearLayout content=new LinearLayout(this);content.setOrientation(1);content.setPadding(24,36,24,24);content.setBackgroundColor(Color.BLACK);
        content.setOnApplyWindowInsetsListener((view,insets)->{
            int left,top,right,bottom;
            if(android.os.Build.VERSION.SDK_INT>=30){android.graphics.Insets value=insets.getInsets(android.view.WindowInsets.Type.systemBars()|android.view.WindowInsets.Type.displayCutout());left=value.left;top=value.top;right=value.right;bottom=value.bottom;}
            else{left=insets.getSystemWindowInsetLeft();top=insets.getSystemWindowInsetTop();right=insets.getSystemWindowInsetRight();bottom=insets.getSystemWindowInsetBottom();}
            int margin=Math.round(16*getResources().getDisplayMetrics().density);view.setPadding(left+margin,top+margin,right+margin,bottom+margin);return insets;
        });
        TextView title=new TextView(this);title.setText("WiizV");title.setTextSize(28);title.setTextColor(Color.WHITE);content.addView(title);
        status=new TextView(this);status.setTextColor(Color.WHITE);status.setTextSize(16);status.setPadding(0,20,0,20);content.addView(status);
        add(content,"保存フォルダを選ぶ（Documents / WiizV）",this::chooseFolder);
        add(content,"ROMを選ぶ",this::chooseRom);
        add(content,"前回のROMで開始",()->startRom(Uri.parse(prefs.getString("rom",""))));
        add(content,"ライセンス",this::licenses);
        setContentView(content);
        if((getApplicationInfo().flags&android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE)!=0&&getIntent().hasExtra("test_rom")){
            Intent game=new Intent(this,GameActivity.class);game.putExtra("rom_path",getIntent().getStringExtra("test_rom"));game.putExtra("smoke",true);
            game.putExtra("native_args",getIntent().getStringArrayExtra("native_args"));startActivity(game);return;
        }
        try{
            String tree=prefs.getString("tree",""),root=prefs.getString("root","");
            if(!tree.isEmpty())store=new DocumentStore(this,Uri.parse(tree),Uri.parse(root));
            if(store!=null&&!prefs.getString("rom","").isEmpty()){startRom(Uri.parse(prefs.getString("rom","")));return;}
            status.setText("Documentsを選ぶと、その中にWiizVを作成します。既存のWiizVを直接選ぶこともできます。設定・セーブ・ステートは選んだ場所へ保存します。");
        }catch(Exception error){failure(error);}
    }
    private void add(LinearLayout parent,String label,Runnable action){Button button=new Button(this);button.setText(label);button.setOnClickListener(v->{if(!busy)action.run();});parent.addView(button,new LinearLayout.LayoutParams(-1,-2));}
    private void failure(Exception error){busy=false;status.setText("読み込み・保存先の確認が必要です。\n"+error.getMessage());}
    private void licenses(){
        StringBuilder value=new StringBuilder();
        for(String name:new String[]{"snes9x/LICENSE","SDL3.txt"})try(BufferedReader reader=new BufferedReader(new InputStreamReader(getAssets().open("licenses/"+name),java.nio.charset.StandardCharsets.UTF_8))){value.append(name).append("\n\n");String line;while((line=reader.readLine())!=null)value.append(line).append('\n');value.append("\n");}catch(IOException error){value.append(error.getMessage());}
        TextView text=new TextView(this);text.setText(value);text.setTextIsSelectable(true);text.setPadding(20,12,20,12);ScrollView scroll=new ScrollView(this);scroll.addView(text);new AlertDialog.Builder(this).setTitle("ライセンス").setView(scroll).setPositiveButton("閉じる",null).show();
    }
    private void chooseFolder(){Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_WRITE_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,Uri.parse("content://com.android.externalstorage.documents/document/primary%3ADocuments"));startActivityForResult(intent,FOLDER);}
    private void chooseRom(){if(store==null){chooseFolder();return;}Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);intent.setType("*/*");intent.addCategory(Intent.CATEGORY_OPENABLE);intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,store.root);startActivityForResult(intent,ROM);}
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);if(result!=RESULT_OK||data==null||data.getData()==null)return;
        try{
            Uri uri=data.getData();int flags=data.getFlags()&(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            getContentResolver().takePersistableUriPermission(uri,flags);
            if(request==FOLDER){store=DocumentStore.choose(this,uri);prefs.edit().putString("tree",uri.toString()).putString("root",store.root.toString()).apply();
                List<DocumentStore.Entry> roms=store.roms();
                if(roms.size()==1)startRom(roms.get(0).uri);
                else if(roms.size()>1){String[] names=new String[roms.size()];for(int i=0;i<names.length;i++)names[i]=roms.get(i).name;new AlertDialog.Builder(this).setTitle("ROMを選択").setItems(names,(d,index)->startRom(roms.get(index).uri)).show();}
                else{status.setText("保存先を選択しました。ROMをDocuments/WiizVへ置くか、ROMを選ぶボタンから指定してください。");chooseRom();}
            } else if(request==ROM)startRom(uri);
        }catch(Exception error){failure(error);}
    }
    static String displayName(Context context,Uri uri) throws IOException {
        try(Cursor cursor=context.getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){
            if(cursor==null||!cursor.moveToFirst())throw new IOException("ROMの名前を読めません。");
            String name=cursor.getString(0);
            if(name==null||name.isEmpty()||name.contains("/")||name.contains("\\")||name.equals(".")||name.equals(".."))throw new IOException("ROMの名前が不正です。");return name;
        }
    }
    static File workDirectory(Context context,String root) throws IOException {
        try{
            byte[] digest=java.security.MessageDigest.getInstance("SHA-256").digest(root.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            StringBuilder key=new StringBuilder();for(byte value:digest)key.append(String.format(Locale.ROOT,"%02x",value&255));
            return new File(context.getFilesDir(),"work/"+key);
        }catch(java.security.NoSuchAlgorithmException error){throw new IOException("保存先を識別できません。",error);}
    }
    private void startRom(Uri uri){
        if(busy)return;if(store==null||uri.toString().isEmpty()){status.setText("保存フォルダとROMを選んでください。");return;}
        busy=true;status.setText("ROMと保存データを読み込んでいます…");
        new Thread(()->{
            try{
                File work=workDirectory(this,store.root.toString());store.hydrate(work);
                String name=displayName(this,uri);String lower=name.toLowerCase(Locale.ROOT);
                if(!lower.endsWith(".smc")&&!lower.endsWith(".sfc"))throw new IOException(".smc または .sfc のROMを選んでください。");
                File rom=new File(getFilesDir(),"rom/"+name);store.copyTo(uri,rom,8*1024*1024+512);
                prefs.edit().putString("rom",uri.toString()).apply();
                runOnUiThread(()->{busy=false;status.setText(name);Intent game=new Intent(this,GameActivity.class);game.putExtra("rom_path",rom.getAbsolutePath());startActivity(game);});
            }catch(Exception error){runOnUiThread(()->failure(error));}
        },"WiizV-storage").start();
    }
}
