package com.wiizv.app;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.util.AtomicFile;
import java.io.*;
import java.util.*;

// SAFのURIと、ネイティブコアが必要とする作業用ファイルを橋渡しする。
final class DocumentStore {
    private final Context context;
    final Uri tree, root;
    private static final int LIMIT=32*1024*1024;
    private static final String BACKUP=".wiizv-backup", TEMP=".wiizv-tmp";
    static final class Entry {
        final Uri uri; final String name,mime;
        Entry(Uri uri,String name,String mime){this.uri=uri;this.name=name;this.mime=mime;}
        boolean directory(){return Document.MIME_TYPE_DIR.equals(mime);}
    }
    DocumentStore(Context context,Uri tree,Uri root) throws IOException {
        this.context=context;this.tree=tree;this.root=root;
        if(!Document.MIME_TYPE_DIR.equals(info(root,Document.COLUMN_MIME_TYPE)))throw new IOException("保存フォルダを開けません。フォルダを選び直してください。");
    }
    static DocumentStore choose(Context context,Uri tree) throws IOException {
        Uri selected=DocumentsContract.buildDocumentUriUsingTree(tree,DocumentsContract.getTreeDocumentId(tree));
        DocumentStore store=new DocumentStore(context,tree,selected);
        if("WiizV".equals(store.info(selected,Document.COLUMN_DISPLAY_NAME)))return store;
        return new DocumentStore(context,tree,store.directory(selected,"WiizV"));
    }
    private String info(Uri uri,String column) throws IOException {
        try(Cursor cursor=context.getContentResolver().query(uri,new String[]{column},null,null,null)) {
            if(cursor==null||!cursor.moveToFirst())throw new IOException("保存先へアクセスできません: "+uri);
            return cursor.getString(0);
        } catch(RuntimeException error){throw new IOException("保存先の権限とフォルダを確認してください。",error);}
    }
    List<Entry> children(Uri parent) throws IOException {
        Uri uri=DocumentsContract.buildChildDocumentsUriUsingTree(tree,DocumentsContract.getDocumentId(parent));
        List<Entry> result=new ArrayList<>();
        try(Cursor cursor=context.getContentResolver().query(uri,new String[]{Document.COLUMN_DOCUMENT_ID,Document.COLUMN_DISPLAY_NAME,Document.COLUMN_MIME_TYPE},null,null,null)){
            if(cursor==null)throw new IOException("フォルダ一覧を取得できません。");
            while(cursor.moveToNext())result.add(new Entry(DocumentsContract.buildDocumentUriUsingTree(tree,cursor.getString(0)),cursor.getString(1),cursor.getString(2)));
        }catch(RuntimeException error){throw new IOException("フォルダ一覧を読み込めません。",error);}
        return result;
    }
    private Entry find(Uri parent,String name) throws IOException {
        for(Entry entry:children(parent))if(entry.name.equals(name))return entry;return null;
    }
    private Uri directory(Uri parent,String name) throws IOException {
        Entry entry=find(parent,name);
        if(entry!=null){if(!entry.directory())throw new IOException("フォルダと同名のファイルがあります: "+name);return entry.uri;}
        Uri created=DocumentsContract.createDocument(context.getContentResolver(),parent,Document.MIME_TYPE_DIR,name);
        if(created==null)throw new IOException("フォルダを作れません: "+name);return created;
    }
    private Uri rename(Uri file,String name) throws IOException {
        Uri result=DocumentsContract.renameDocument(context.getContentResolver(),file,name);
        if(result==null)throw new IOException("安全なファイル置換に対応しない保存先です。端末内のDocumentsを選んでください。");return result;
    }
    private void recover(Uri parent) throws IOException {
        for(Entry entry:children(parent))if(!entry.directory()&&entry.name.endsWith(BACKUP)){
            String name=entry.name.substring(0,entry.name.length()-BACKUP.length());
            if(find(parent,name)==null)rename(entry.uri,name);
        }
    }
    private Uri resolveParent(String[] parts) throws IOException {
        Uri parent=root;
        for(int i=0;i<parts.length-1;i++)parent=directory(parent,parts[i]);return parent;
    }
    synchronized void write(String relative,byte[] bytes) throws IOException {
        if(bytes.length>LIMIT)throw new IOException("保存データが大きすぎます。");
        String[] parts=relative.split("/",-1);
        for(String part:parts)if(part.isEmpty()||part.equals(".")||part.equals("..")||part.indexOf('\\')>=0)throw new IOException("保存先のパスが不正です。");
        Uri parent=resolveParent(parts);String name=parts[parts.length-1];recover(parent);
        Entry old=find(parent,name), leftover=find(parent,name+TEMP), backup=find(parent,name+BACKUP);
        if(leftover!=null&&!DocumentsContract.deleteDocument(context.getContentResolver(),leftover.uri))throw new IOException("以前の一時ファイルを削除できません。");
        if(backup!=null&&!DocumentsContract.deleteDocument(context.getContentResolver(),backup.uri))throw new IOException("以前の保存バックアップを整理できません。");
        Uri temporary=DocumentsContract.createDocument(context.getContentResolver(),parent,"application/octet-stream",name+TEMP);
        if(temporary==null)throw new IOException("保存先へ一時ファイルを作れません: "+name);
        Uri oldBackup=null;
        try {
            ParcelFileDescriptor descriptor=context.getContentResolver().openFileDescriptor(temporary,"rwt");
            if(descriptor==null)throw new IOException("一時ファイルを開けません。");
            try(ParcelFileDescriptor.AutoCloseOutputStream stream=new ParcelFileDescriptor.AutoCloseOutputStream(descriptor)){
                stream.write(bytes);stream.flush();stream.getFD().sync();
            }
            if(old!=null)oldBackup=rename(old.uri,name+BACKUP);
            rename(temporary,name);
        } catch(Exception error){
            if(oldBackup!=null)try{rename(oldBackup,name);}catch(Exception recovery){error.addSuppressed(recovery);}
            try{DocumentsContract.deleteDocument(context.getContentResolver(),temporary);}catch(Exception ignored){}
            throw new IOException("保存に失敗しました。既存データを確認してください: "+relative,error);
        }
        if(oldBackup!=null)try{DocumentsContract.deleteDocument(context.getContentResolver(),oldBackup);}catch(Exception ignored){}
    }
    void hydrate(File work) throws IOException {
        if(!work.isDirectory()&&!work.mkdirs())throw new IOException("作業フォルダを作れません。");
        for(String name:new String[]{"Saves","States"})copyDirectory(directory(root,name),new File(work,name),0);
        Entry settings=find(root,"settings.ini");
        if(settings!=null&&!settings.directory())copyTo(settings.uri,new File(work,"settings.ini"),LIMIT);
        else {File cached=new File(work,"settings.ini");if(cached.exists()&&!cached.delete())throw new IOException("古い作業用設定を削除できません。");}
    }
    private void copyDirectory(Uri source,File target,int depth) throws IOException {
        if(depth>3)throw new IOException("保存フォルダの階層が深すぎます。");
        if(!target.isDirectory()&&!target.mkdirs())throw new IOException("作業フォルダを作れません: "+target);
        recover(source);
        Set<String> present=new HashSet<>();
        for(Entry entry:children(source)){
            if(entry.name.contains("/")||entry.name.contains("\\")||entry.name.equals(".."))throw new IOException("不正なファイル名があります。");
            if(entry.name.endsWith(TEMP)||entry.name.endsWith(BACKUP)||entry.name.endsWith(".tmp"))continue;
            present.add(entry.name);
            File destination=new File(target,entry.name);
            if(entry.directory())copyDirectory(entry.uri,destination,depth+1);else copyTo(entry.uri,destination,LIMIT);
        }
        File[] cached=target.listFiles();if(cached==null)throw new IOException("作業フォルダを確認できません。");
        for(File file:cached)if(!present.contains(file.getName()))removeCached(file,target);
    }
    private void removeCached(File file,File allowed) throws IOException {
        if(!file.getCanonicalPath().startsWith(allowed.getCanonicalPath()+"/"))throw new IOException("作業フォルダの範囲を超えています。");
        if(file.isDirectory()){File[] children=file.listFiles();if(children==null)throw new IOException("作業フォルダを確認できません。");for(File child:children)removeCached(child,allowed);}
        if(!file.delete())throw new IOException("古い作業ファイルを削除できません。");
    }
    void copyTo(Uri source,File destination,int limit) throws IOException {
        File parent=destination.getParentFile();if(!parent.isDirectory()&&!parent.mkdirs())throw new IOException("作業フォルダを作れません。");
        AtomicFile atomic=new AtomicFile(destination);FileOutputStream output=null;
        try(InputStream input=context.getContentResolver().openInputStream(source)){
            if(input==null)throw new IOException("ファイルを読めません。");output=atomic.startWrite();
            byte[] buffer=new byte[65536];int count,total=0;
            while((count=input.read(buffer))!=-1){total+=count;if(total>limit)throw new IOException("ファイルサイズが大きすぎます。");output.write(buffer,0,count);}
            atomic.finishWrite(output);output=null;
        } finally {if(output!=null)atomic.failWrite(output);}
    }
    List<Entry> roms() throws IOException {
        List<Entry> result=new ArrayList<>();
        for(Entry entry:children(root)){
            if(entry.directory()&&entry.name.equalsIgnoreCase("Rom")){for(Entry child:children(entry.uri))if(isRom(child))result.add(child);}
            else if(isRom(entry))result.add(entry);
        }
        return result;
    }
    private boolean isRom(Entry entry){String name=entry.name.toLowerCase(Locale.ROOT);return !entry.directory()&&(name.endsWith(".smc")||name.endsWith(".sfc"));}
}
