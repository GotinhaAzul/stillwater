from pathlib import Path
import os,subprocess,tempfile,time,signal
root=Path(__file__).resolve().parent
binary=root.parent/'native/build/stillwater-native'
for scene in ('pond','sky','mountains'):
 with tempfile.TemporaryDirectory(prefix='stillwater-wayland-test-') as directory:
  env=os.environ.copy();env.update(XDG_RUNTIME_DIR=directory,WAYLAND_DISPLAY='stillwater-test')
  server=subprocess.Popen([str(root/'build/wayland-fixture')],cwd=directory,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  assert server.stdout.readline().strip()=='READY'
  client=subprocess.Popen([str(binary),'--scene',scene],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  try:
   time.sleep(.45);assert client.poll() is None
   client.send_signal(signal.SIGUSR1);time.sleep(.15)
   frames=Path(directory)/'frames';a=frames.read_text();time.sleep(.3);assert frames.read_text()==a,'Pause failed'
   client.send_signal(signal.SIGUSR1)
   stdout,stderr=server.communicate(timeout=8);assert server.returncode==0,stderr
   assert 'PASS:' in stdout,stdout+stderr
   print(scene+': '+stdout.strip()+' Pause/resume passed.')
  finally:
   if client.poll() is None:client.terminate()
   client.communicate(timeout=2)
   if server.poll() is None:server.terminate();server.communicate(timeout=2)
print('PASS: actual Wayland client tested without connecting to or changing your desktop.')
