package com.wiizv.app;

import android.app.*;
import android.content.*;
import android.content.res.Configuration;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.view.*;
import android.widget.*;
import org.libsdl.app.SDLActivity;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public final class GameActivity extends SDLActivity {
    private DocumentStore storage;
    private PadMenuDialog menu;
    private int page,serial,closedSerial=-1,selectedActor=0,goldStep=4,experienceSteps=1,alignment=0;
    private static final String[] ALIGNMENTS={"善 Good","中立 Neutral","悪 Evil","不明"};
    private static final long[] GOLD={1,10,100,1000,10000,100000,1000000,10000000,100000000,1000000000,10000000000L,100000000000L,999999999999L};
    private static final String[] SPEED={"1倍","1.5倍","2倍","3倍","4倍"};
    private final List<Actor> actors=new ArrayList<>();
    private int[] flags;
    private static native boolean postCommand(byte[] utf8);
    @Override protected String[] getLibraries(){return new String[]{"SDL3","main"};}
    @Override protected String[] getArguments(){
        if(getIntent().getBooleanExtra("smoke",false)){
            String[] extra=getIntent().getStringArrayExtra("native_args");if(extra!=null)return extra;
            return new String[]{"--smoke",getIntent().getStringExtra("rom_path"),"--data-dir",dataDirectory(),"--frames","900"};
        }
        return new String[]{"--data-dir",dataDirectory()};
    }
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        if(!getIntent().getBooleanExtra("smoke",false))try{
            android.content.SharedPreferences prefs=getSharedPreferences("startup",MODE_PRIVATE);
            storage=new DocumentStore(this,Uri.parse(prefs.getString("tree","")),Uri.parse(prefs.getString("root","")));
        }catch(Exception error){Toast.makeText(this,error.getMessage(),Toast.LENGTH_LONG).show();finish();return;}
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        updateSystemBars();
    }
    public String nativeDirectory(){return getApplicationInfo().nativeLibraryDir;}
    public String dataDirectory(){
        if(getIntent().getBooleanExtra("smoke",false))return new File(getFilesDir(),"test").getAbsolutePath();
        try{return LauncherActivity.workDirectory(this,getSharedPreferences("startup",MODE_PRIVATE).getString("root","")).getAbsolutePath();}
        catch(IOException error){throw new IllegalStateException(error);}
    }
    public String romPath(){return getIntent().getStringExtra("rom_path");}
    public float displayDensity(){return getResources().getDisplayMetrics().density;}
    public boolean hasPhysicalGamepad(){
        for(int id:InputDevice.getDeviceIds()){
            InputDevice device=InputDevice.getDevice(id);if(device==null||device.isVirtual())continue;
            int sources=device.getSources();
            if((sources&InputDevice.SOURCE_GAMEPAD)==InputDevice.SOURCE_GAMEPAD||(sources&InputDevice.SOURCE_JOYSTICK)==InputDevice.SOURCE_JOYSTICK)return true;
        }
        return false;
    }
    public void commitFile(String path,byte[] bytes) throws IOException {
        if(getIntent().getBooleanExtra("smoke",false))return;
        if(storage==null)throw new IOException("保存フォルダが選択されていません。");
        String root=new File(dataDirectory()).getCanonicalPath()+"/",file=new File(path).getCanonicalPath();
        if(!file.startsWith(root))throw new IOException("保存先の範囲を超えています。");
        storage.write(file.substring(root.length()),bytes);
    }
    public int[] renderUiText(String text){
        Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);paint.setTypeface(Typeface.MONOSPACE);paint.setTextSize(16);paint.setColor(Color.rgb(235,239,225));
        Paint.FontMetrics fm=paint.getFontMetrics();int width=Math.max(1,(int)Math.ceil(paint.measureText(text))),height=(int)Math.ceil(fm.descent-fm.ascent);
        Bitmap image=Bitmap.createBitmap(width,height,Bitmap.Config.ARGB_8888);new Canvas(image).drawText(text,0,-fm.ascent,paint);
        int[] result=new int[2+width*height];result[0]=width;result[1]=height;image.getPixels(result,2,width,0,0,width,height);image.recycle();return result;
    }
    public void showNotice(String text){runOnUiThread(()->Toast.makeText(this,text,Toast.LENGTH_LONG).show());}
    public void chooseRom(){runOnUiThread(()->{
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);intent.addCategory(Intent.CATEGORY_OPENABLE);intent.setType("*/*");intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);startActivityForResult(intent,30);
    });}
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);if(request!=30||result!=RESULT_OK||data==null||data.getData()==null)return;
        Uri uri=data.getData();
        new Thread(()->{try{
            getContentResolver().takePersistableUriPermission(uri,data.getFlags()&Intent.FLAG_GRANT_READ_URI_PERMISSION);
            String name=LauncherActivity.displayName(this,uri);String lower=name.toLowerCase(Locale.ROOT);
            if(!lower.endsWith(".smc")&&!lower.endsWith(".sfc"))throw new IOException(".smc または .sfc のROMを選んでください。");
            File file=new File(getFilesDir(),"rom/"+name);storage.copyTo(uri,file,8*1024*1024+512);
            getSharedPreferences("startup",MODE_PRIVATE).edit().putString("rom",uri.toString()).apply();
            send("rom "+file.getAbsolutePath());
        }catch(Exception error){showNotice(error.getMessage());}},"WiizV-ROM").start();
    }
    private boolean commandPending;
    private String lastSnapshot="";
    private final Map<Integer,Bitmap> nameCache=new HashMap<>();
    private int dp(float value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private void send(String command){runOnUiThread(()->{
        boolean closing=command.equals("close");
        if(commandPending&&!closing)return;
        commandPending=!closing;
        if(menu!=null)menu.setBusy(commandPending);
        if(!postCommand(command.getBytes(StandardCharsets.UTF_8))){commandPending=false;if(menu!=null)menu.setBusy(false);showNotice("操作を送れません。ゲームを再開してから試してください。");}
    });}
    public void dismissFunctionMenu(){closedSerial=serial;commandPending=false;send("close");if(menu!=null)menu.dismiss();}
    private void closeMenu(){dismissFunctionMenu();}
    private void backFunctionMenu(){if(page==1)send("root");else dismissFunctionMenu();}
    public void showFunctionMenu(String value){runOnUiThread(()->{
        try{
            String[] lines=value.split("\n"),header=lines[0].split("\\|");int incoming=Integer.parseInt(header[10]);
            if(incoming<=closedSerial)return;
            boolean newMenu=menu==null||!menu.isShowing();int previousPage=page;
            page=Integer.parseInt(header[0]);serial=incoming;flags=new int[header.length];for(int i=0;i<header.length;++i)flags[i]=Integer.parseInt(header[i]);
            commandPending=false;
            if(!value.equals(lastSnapshot)){
                actors.clear();for(int i=1;i<lines.length;++i){Actor actor=new Actor(lines[i]);nameCache.put(actor.id,actor.name);actors.add(actor);}
                lastSnapshot=value;
            }
            if(previousPage!=page&&page==1&&!actors.isEmpty())alignment=currentActor().alignment%3;
            if(newMenu){
                menu=new PadMenuDialog(this,this::backFunctionMenu);
                menu.setOnDismissListener(d->{closedSerial=serial;commandPending=false;send("close");});
            }
            renderFunctionRows();if(newMenu)menu.show();menu.resize();
        }catch(Exception error){commandPending=false;showNotice("メニューを更新できません: "+error.getMessage());}
    });}
    public void menuKey(int action){runOnUiThread(()->{if(menu!=null&&menu.isShowing())menu.navigate(action);});}
    private void add(List<PadMenuDialog.Item> items,String id,CharSequence label,boolean enabled,Runnable execute,PadMenuDialog.Adjust adjust){items.add(new PadMenuDialog.Item(id,label,enabled,execute,adjust));}
    private void toggle(List<PadMenuDialog.Item> items,String id,String label,boolean enabled,String command){add(items,id,label+"  "+(enabled?"ON":"OFF"),true,()->send(command),direction->send(command));}
    private void renderFunctionRows(){
        if(menu==null)return;List<PadMenuDialog.Item> items=new ArrayList<>();
        if(page==0){
            add(items,"resume","ゲームにもどる",true,this::dismissFunctionMenu,null);
            for(int slot=1;slot<=5;++slot){final int index=slot;boolean occupied=(flags[8]&(1<<(slot-1)))!=0;
                add(items,"save"+slot,"スロット"+slot+"  保存",true,()->send("save "+index),null);
                add(items,"load"+slot,"スロット"+slot+(occupied?"  読込":"  空き"),occupied,()->{dismissFunctionMenu();send("load "+index);},null);
            }
            add(items,"speed","倍速  "+SPEED[flags[1]],true,()->send("speed "+((flags[1]+1)%5)),direction->send("speed "+((flags[1]+direction+5)%5)));
            toggle(items,"swap","ABXYいれかえ（A/B・X/Y）",flags[2]!=0,"swap");
            toggle(items,"bonus","新規キャラクター ボーナス最大",flags[3]!=0,"bonus");
            toggle(items,"logo","メーカーロゴをスキップ",flags[4]!=0,"logo");
            toggle(items,"mixed","酒場で善悪混合を許可",flags[5]!=0,"mixed");
            toggle(items,"crt","CRT表示",flags[6]!=0,"crt");
            String[] effects={"あかり LOMILWA","ほご MAPORFIC","しきべつ LATUMAPIC","ふゆう LITOFEITO","MP9 / 呪文消費なし"};
            for(int i=0;i<effects.length;++i)toggle(items,"effect"+i,effects[i],(flags[7]&(1<<i))!=0,"effect "+i);
            add(items,"character","キャラクターの編集",flags[9]!=0,()->send("character"),null);
            add(items,"reset","ソフトを再起動",true,()->{dismissFunctionMenu();send("reset");},null);
        }else{
            add(items,"root","機能メニューにもどる",true,()->send("root"),null);
            if(!actors.isEmpty()){
                Actor actor=currentActor();
                add(items,"actor",actorLabel(actor),true,()->chooseActor(1),this::chooseActor);
                add(items,"goldStep","GOLD加算量  +"+number(GOLD[goldStep]),true,()->changeGoldStep(1),this::changeGoldStep);
                add(items,"gold","GOLDを加算  現在 "+number(actor.gold),actor.gold>=0,()->send("gold "+selectedActor+" "+goldStep),null);
                add(items,"expStep","経験値  次の＋"+experienceSteps+"段階へ",true,()->changeExperienceStep(1),this::changeExperienceStep);
                add(items,"exp","経験値を加算  "+number(actor.experience)+" → "+number(actor.targets[experienceSteps-1]),actor.experience>=0&&actor.targets[0]>=0,()->send("exp "+selectedActor+" "+experienceSteps),null);
                add(items,"alignmentChoice","変更先の属性  "+ALIGNMENTS[alignment],true,()->changeAlignment(1),this::changeAlignment);
                add(items,"alignment","属性を変更  現在 "+ALIGNMENTS[Math.min(3,actor.alignment)],true,()->send("alignment "+selectedActor+" "+alignment),null);
                add(items,"spells",actor.spells?"全てのスペルを習得済み":"全てのスペルを覚える",!actor.spells,()->send("spells "+selectedActor),null);
                add(items,"undo","編集前のゲーム状態へ戻す",true,()->send("undo "+selectedActor),null);
            }
        }
        menu.update(page,items,flags[2]!=0);
    }
    private Actor currentActor(){for(Actor actor:actors)if(actor.id==selectedActor)return actor;selectedActor=actors.get(0).id;return actors.get(0);}
    private void chooseActor(int direction){int current=0;for(int i=0;i<actors.size();++i)if(actors.get(i).id==selectedActor)current=i;Actor actor=actors.get((current+direction+actors.size())%actors.size());selectedActor=actor.id;alignment=actor.alignment%3;renderFunctionRows();}
    private void changeGoldStep(int direction){goldStep=(goldStep+direction+GOLD.length)%GOLD.length;renderFunctionRows();}
    private void changeExperienceStep(int direction){experienceSteps=(experienceSteps-1+direction+3)%3+1;renderFunctionRows();}
    private void changeAlignment(int direction){alignment=(alignment+direction+3)%3;renderFunctionRows();}
    private CharSequence actorLabel(Actor actor){
        android.text.SpannableStringBuilder label=new android.text.SpannableStringBuilder("#"+(actor.id+1)+"  ");int start=label.length();label.append(" ");
        android.graphics.drawable.BitmapDrawable drawable=new android.graphics.drawable.BitmapDrawable(getResources(),actor.name);drawable.setFilterBitmap(false);drawable.setBounds(0,0,dp(128),dp(16));
        label.setSpan(new android.text.style.ImageSpan(drawable,android.text.style.ImageSpan.ALIGN_BASELINE),start,start+1,android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        label.append("  "+ALIGNMENTS[Math.min(3,actor.alignment)]+" Lv"+actor.level);return label;
    }
    private String number(long value){return value<0?"不正データ":String.format(Locale.JAPAN,"%,d",value);}
    private void updateSystemBars(){
        boolean landscape=getResources().getConfiguration().orientation==Configuration.ORIENTATION_LANDSCAPE;
        if(android.os.Build.VERSION.SDK_INT>=30){WindowInsetsController controller=getWindow().getInsetsController();if(controller!=null){controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);if(landscape)controller.hide(WindowInsets.Type.systemBars());else controller.show(WindowInsets.Type.systemBars());}}
        else getWindow().getDecorView().setSystemUiVisibility(landscape?View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY:View.SYSTEM_UI_FLAG_VISIBLE);
    }
    @Override public void onConfigurationChanged(Configuration configuration){super.onConfigurationChanged(configuration);updateSystemBars();if(menu!=null){menu.resize();getWindow().getDecorView().post(()->{if(menu!=null)menu.resize();});}}
    static final class Actor {
        final int id,alignment,level;final long gold,experience;final long[] targets=new long[3];final Bitmap name;final boolean spells;
        Actor(String line){String[] f=line.split("\\|");id=Integer.parseInt(f[0]);gold=Long.parseLong(f[2]);experience=Long.parseLong(f[3]);alignment=Integer.parseInt(f[4]);level=Integer.parseInt(f[5]);for(int i=0;i<3;i++)targets[i]=Long.parseLong(f[6+i]);
            spells=f.length>9&&f[9].equals("1");
            name=Bitmap.createBitmap(64,8,Bitmap.Config.ARGB_8888);for(int letter=0;letter<8;letter++)for(int row=0;row<8;row++){int bits=Integer.parseInt(f[1].substring((letter*8+row)*2,(letter*8+row)*2+2),16);for(int x=0;x<8;x++)if((bits&(128>>x))!=0)name.setPixel(letter*8+x,row,Color.WHITE);}
        }
    }
}
