package com.wiizv.app;

import android.app.Dialog;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.Rect;
import android.graphics.drawable.GradientDrawable;
import android.view.*;
import android.widget.*;
import java.util.*;
import org.libsdl.app.SDLControllerManager;

// 選択位置を維持し、表示矩形はゲーム描画領域に合わせる。
final class PadMenuDialog extends Dialog {
    interface Adjust { void accept(int direction); }
    static final class Item {
        final String id;final CharSequence label;final boolean enabled;final Runnable execute;final Adjust adjust;
        Item(String id,CharSequence label,boolean enabled,Runnable execute,Adjust adjust){this.id=id;this.label=label;this.enabled=enabled;this.execute=execute;this.adjust=adjust;}
    }
    private final GameActivity activity;
    private final Runnable back;
    private final LinearLayout body;
    private final ScrollView scroll;
    private final TextView title;
    private final List<Button> buttons=new ArrayList<>();
    private List<Item> items=new ArrayList<>();
    private int selected,page=-1;
    private final Map<Integer,String> selections=new HashMap<>();
    private boolean busy;
    private boolean swap;
    PadMenuDialog(GameActivity activity,Runnable back){
        super(activity);this.activity=activity;this.back=back;requestWindowFeature(Window.FEATURE_NO_TITLE);
        LinearLayout panel=new LinearLayout(activity);panel.setOrientation(1);panel.setBackgroundColor(Color.BLACK);
        GradientDrawable frame=new GradientDrawable();frame.setColor(Color.BLACK);frame.setStroke(dp(1),Color.WHITE);panel.setBackground(frame);panel.setPadding(dp(2),dp(2),dp(2),dp(2));
        LinearLayout header=new LinearLayout(activity);title=new TextView(activity);title.setTextColor(Color.WHITE);title.setTextSize(18);title.setSingleLine(true);title.setEllipsize(android.text.TextUtils.TruncateAt.END);title.setPadding(dp(8),dp(6),dp(8),dp(6));
        header.addView(title,new LinearLayout.LayoutParams(0,-2,1));panel.addView(header);
        scroll=new ScrollView(activity);scroll.setFillViewport(false);body=new LinearLayout(activity);body.setOrientation(1);body.setPadding(dp(8),0,dp(8),dp(4));scroll.addView(body);panel.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        setContentView(panel);setCanceledOnTouchOutside(false);
        Window window=getWindow();
        window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
            |WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN|WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
        window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        window.setBackgroundDrawableResource(android.R.color.transparent);
        window.getDecorView().setPadding(0,0,0,0);
        if(android.os.Build.VERSION.SDK_INT>=30){
            window.setDecorFitsSystemWindows(false);
            WindowManager.LayoutParams attributes=window.getAttributes();
            attributes.setFitInsetsTypes(0);
            attributes.layoutInDisplayCutoutMode=WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            window.setAttributes(attributes);
        }
    }
    private int dp(int value){return Math.round(value*activity.getResources().getDisplayMetrics().density);}
    private Button control(String text,Runnable action){Button button=new Button(activity);button.setText(text);button.setTextSize(14);button.setTextColor(Color.WHITE);button.setFocusable(false);button.setBackgroundTintList(android.content.res.ColorStateList.valueOf(Color.rgb(24,30,24)));button.setOnClickListener(v->action.run());return button;}
    void resize(){
        Rect area=activity.functionMenuBounds();
        if(getWindow()==null||area.isEmpty())return;
        WindowManager.LayoutParams layout=getWindow().getAttributes();
        if(layout.gravity==(Gravity.TOP|Gravity.LEFT)&&layout.x==area.left&&layout.y==area.top
            &&layout.width==area.width()&&layout.height==area.height())return;
        layout.gravity=Gravity.TOP|Gravity.LEFT;layout.x=area.left;layout.y=area.top;
        layout.width=area.width();layout.height=area.height();getWindow().setAttributes(layout);
    }
    void update(int newPage,List<Item> next,boolean swapped){
        if(selected<items.size())selections.put(page,items.get(selected).id);
        boolean changed=page!=newPage;
        String id=selections.containsKey(newPage)?selections.get(newPage):"";
        page=newPage;swap=swapped;items=next;selected=0;
        for(int i=0;i<items.size();++i)if(items.get(i).id.equals(id))selected=i;
        while(buttons.size()<items.size()){
            final int index=buttons.size();Button button=control("",()->{if(index>=items.size())return;selected=index;paintSelection(false);activate();});button.setGravity(Gravity.LEFT|Gravity.CENTER_VERTICAL);button.setAllCaps(false);button.setTypeface(Typeface.MONOSPACE);button.setMinHeight(dp(32));button.setMinimumHeight(dp(32));button.setPadding(dp(4),dp(2),dp(4),dp(2));
            buttons.add(button);body.addView(button,new LinearLayout.LayoutParams(-1,-2));
        }
        for(int i=0;i<buttons.size();++i)buttons.get(i).setVisibility(i<items.size()?View.VISIBLE:View.GONE);
        setBusy(false);paintSelection(changed);
    }
    void setBusy(boolean value){busy=value;title.setText(busy?"処理中…":page==1?"キャラクター編集":page==2?"ステート保存":page==3?"ステート読込":"機能メニュー");}
    private void paintSelection(boolean reveal){
        for(int i=0;i<items.size();++i){Button button=buttons.get(i);Item item=items.get(i);android.text.SpannableStringBuilder text=new android.text.SpannableStringBuilder(i==selected?"▶ ":"   ");text.append(item.label);button.setText(text);button.setEnabled(item.enabled);
            GradientDrawable background=new GradientDrawable();background.setColor(Color.BLACK);button.setBackgroundTintList(null);button.setBackground(background);
        }
        if(reveal&&!items.isEmpty()){Button button=buttons.get(selected);button.post(()->button.requestRectangleOnScreen(new Rect(0,0,button.getWidth(),button.getHeight()),true));}
    }
    private void activate(){if(busy||items.isEmpty()||selected>=items.size())return;Item item=items.get(selected);if(item.enabled&&item.execute!=null)item.execute.run();}
    void navigate(int action){
        if(!isShowing())return;
        if(action==1){back.run();return;}if(busy)return;
        if(action==0){activate();return;}if(items.isEmpty())return;
        if(action==2||action==3){
            int direction=action==2?-1:1;
            for(int count=0;count<items.size();++count){selected=(selected+direction+items.size())%items.size();if(items.get(selected).enabled)break;}
            paintSelection(true);
        }else if(action==4||action==5){Item item=items.get(selected);if(item.enabled&&item.adjust!=null)item.adjust.accept(action==4?-1:1);}
    }
    @Override public boolean dispatchKeyEvent(KeyEvent event){
        int code=event.getKeyCode();
        // 実パッドはゲームと同じSDLマッピングを経由させ、ProconのA/B位置も統一する。
        if(SDLControllerManager.isDeviceSDLJoystick(event.getDeviceId())&&code!=KeyEvent.KEYCODE_BACK){
            boolean handled=event.getAction()==KeyEvent.ACTION_DOWN?SDLControllerManager.onNativePadDown(event.getDeviceId(),code):SDLControllerManager.onNativePadUp(event.getDeviceId(),code);
            if(handled)return true;
        }
        int action=-1;
        switch(code){
            case KeyEvent.KEYCODE_DPAD_UP:action=2;break;case KeyEvent.KEYCODE_DPAD_DOWN:action=3;break;
            case KeyEvent.KEYCODE_DPAD_LEFT:action=4;break;case KeyEvent.KEYCODE_DPAD_RIGHT:action=5;break;
            case KeyEvent.KEYCODE_ENTER:case KeyEvent.KEYCODE_DPAD_CENTER:action=0;break;
            case KeyEvent.KEYCODE_BACK:case KeyEvent.KEYCODE_ESCAPE:action=1;break;
            case KeyEvent.KEYCODE_BUTTON_A:action=swap?0:1;break;case KeyEvent.KEYCODE_BUTTON_B:action=swap?1:0;break;
            default:break;
        }
        if(action>=0){if(event.getAction()==KeyEvent.ACTION_DOWN&&((action==2||action==3)||event.getRepeatCount()==0))navigate(action);return true;}
        return super.dispatchKeyEvent(event);
    }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent event){
        if((event.getSource()&InputDevice.SOURCE_JOYSTICK)==InputDevice.SOURCE_JOYSTICK&&SDLControllerManager.handleJoystickMotionEvent(event))return true;
        return super.dispatchGenericMotionEvent(event);
    }
}
