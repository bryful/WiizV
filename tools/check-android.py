"""専用エミュレーターだけでAPKと既存コア機能を検証する。ROMは配布物に含めない。"""
import argparse
import pathlib
import os
import subprocess
import time

parser=argparse.ArgumentParser()
parser.add_argument('--rom',required=True,type=pathlib.Path)
parser.add_argument('--serial',default='emulator-5580')
parser.add_argument('--check',default='smoke',choices=['smoke','states','gold','experience','character-alignment','alignment','movement'])
args=parser.parse_args()
if not args.serial.startswith('emulator-'):raise SystemExit('専用エミュレーターを指定してください。実機はこのスクリプトの対象外です。')
root=pathlib.Path(__file__).resolve().parents[1]
adb=pathlib.Path(os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT') or str(pathlib.Path.home()/'AppData/Local/Android/Sdk'))/'platform-tools/adb.exe'
package='com.wiizv.app'
def run(*command,check=True):
    return subprocess.run([str(adb),'-s',args.serial,*map(str,command)],check=check,capture_output=True,text=True,encoding='utf-8',errors='replace')
output=root/'verification'/'android-build'/('check-'+args.check)
output.mkdir(parents=True,exist_ok=True)
print(run('install','-r',root/'android/app/build/outputs/apk/debug/app-debug.apk').stdout,flush=True)
run('push',args.rom.resolve(),'/data/local/tmp/wiizv-test.smc')
run('shell','run-as',package,'mkdir','-p','files')
run('shell','run-as',package,'cp','/data/local/tmp/wiizv-test.smc','files/test-ROM.smc')
rom='/data/user/0/'+package+'/files/test-ROM.smc'
data='/data/user/0/'+package+'/files/test-'+args.check+'-'+str(int(time.time()))
flags={'states':'--state-check','gold':'--gold-check','experience':'--experience-check','character-alignment':'--character-alignment-check','alignment':'--alignment-check','movement':'--movement-check'}
reports={'states':'state','gold':'gold','experience':'experience','character-alignment':'character-alignment','alignment':'alignment','movement':'movement'}
def launch(extra,report):
    run('shell','am','force-stop',package)
    run('shell','run-as',package,'rm','-f',data+'/'+report,check=False)
    frames='60' if '--alignment-resume-check' in extra else '900'
    command=['--smoke',rom,'--data-dir',data,'--frames',frames,*extra]
    result=run('shell','am','start','-f','0x10008000','-n',package+'/.LauncherActivity','--es','test_rom',rom,'--esa','native_args',','.join(command))
    print(result.stdout,flush=True)
    for _ in range(120):
        result=run('shell','run-as',package,'cat',data+'/'+report,check=False)
        if result.returncode==0:
            (output/report).write_text(result.stdout,encoding='utf-8');print(result.stdout,flush=True);return result.stdout
        time.sleep(1)
    log=run('logcat','-d','-s','SDL','SDL/APP','AndroidRuntime','DEBUG',check=False).stdout
    (output/'failure-logcat.txt').write_text(log,encoding='utf-8')
    raise SystemExit('Android検証が完了しません: '+str(output/'failure-logcat.txt'))
if args.check=='smoke':
    launch([],'smoke-report.txt')
    launch([],'smoke-report.txt')
else:
    launch([flags[args.check]],reports[args.check]+'-check-report.txt')
    if args.check not in ('movement',):
        flag='--state-resume-check' if args.check=='states' else '--'+args.check+'-resume-check'
        extra=[flag]
        if args.check=='alignment':extra+=['--start-state',data+'/States/test-ROM.slot1.wvs']
        launch(extra,reports[args.check]+'-resume-report.txt')
print('PASS: Android '+args.check+'; '+str(output),flush=True)
