#!/usr/bin/env python3
import json,pathlib,socket,subprocess,sys,threading,hashlib
exe=str(pathlib.Path(sys.argv[1]).resolve());out=pathlib.Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
if '--interface-ready' not in sys.argv:subprocess.run(['ip','link','set','lo','up'],check=True)
ip='::1' if '--ipv6' in sys.argv else '127.0.0.1';family=socket.AF_INET6 if '--ipv6' in sys.argv else socket.AF_INET
messages=[b'',b'A',b'repeated',b'repeated',bytes(range(256))*5,bytes(range(250))*128]
errors=[]
def server(bad=False):
 s=socket.socket(family,socket.SOCK_DGRAM);s.bind((ip,0));s.settimeout(8);port=s.getsockname()[1]
 def run():
  try:
   with s:
    for expected in messages:
     b,peer=s.recvfrom(65536);assert b==expected
     s.sendto(b'BAD' if bad else b[::-1]+b'R',peer)
     if bad:break
  except Exception as e:errors.append(str(e))
 t=threading.Thread(target=run);t.start();return port,t

def client(port):
 with socket.socket(family,socket.SOCK_DGRAM) as s:
  s.settimeout(8);s.connect((ip,port))
  for b in messages:s.send(b);assert s.recv(65536)==b[::-1]+b'R'

def run(*args,fail=False):
 r=subprocess.run([exe,*map(str,args)],capture_output=True,text=True,timeout=15)
 assert (r.returncode!=0 if fail else r.returncode==0),(r.stdout,r.stderr)
 return {'args':list(map(str,args)),'rc':r.returncode,'stdout':r.stdout,'stderr':r.stderr}
port,t=server();pcap=out/'udp.pcap'
p=subprocess.Popen([exe,'record','lo',ip,str(port),str(pcap),'3','udp'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
assert p.stdout.readline().strip()=='CAPTURING';client(port);t.join(10);stdout,stderr=p.communicate(timeout=8);assert p.returncode==0,(stdout,stderr)
results=[{'record':stdout},run('inspect',pcap,ip,port)]
assert json.loads(results[-1]['stdout'])['events']==len(messages)*2
port2,t=server();results.append(run('replay-client',pcap,ip,port,ip,port2));t.join(10)
s=socket.socket(family,socket.SOCK_DGRAM);s.bind((ip,0));port3=s.getsockname()[1];s.close()
p=subprocess.Popen([exe,'replay-server',str(pcap),ip,str(port),ip,str(port3)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
assert p.stdout.readline().strip()=='LISTENING';client(port3);stdout,stderr=p.communicate(timeout=10);assert p.returncode==0,(stdout,stderr);results.append({'replay_server':stdout})
port4,t=server(True);results.append(run('replay-client',pcap,ip,port,ip,port4,fail=True));t.join(10)
assert not errors,errors
summary={'host':socket.gethostname(),'ip':ip,'request_datagram_sizes':[len(b) for b in messages],'response_datagram_sizes':[len(b)+1 for b in messages],'pcap_sha256':hashlib.sha256(pcap.read_bytes()).hexdigest(),'checks':results}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
