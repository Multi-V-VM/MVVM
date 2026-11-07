#!/usr/bin/env python3
"""Run as root inside a fresh network namespace; only test loopback is captured."""
import json, pathlib, socket, subprocess, sys, threading, struct, hashlib
exe=str(pathlib.Path(sys.argv[1]).resolve()); out=pathlib.Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
if '--interface-ready' not in sys.argv:
 subprocess.run(['ip','link','set','lo','up'],check=True)
IP='::1' if '--ipv6' in sys.argv else '127.0.0.1'
FAMILY=socket.AF_INET6 if '--ipv6' in sys.argv else socket.AF_INET
payload=bytes(range(256))*1024
response=bytes(x^0x5a for x in payload)+b'END\x00'
errors=[]
def server(bad=False):
 s=socket.socket(FAMILY);s.bind((IP,0));s.listen();port=s.getsockname()[1]
 def run():
  try:
   with s, s.accept()[0] as c:
    c.settimeout(8);data=bytearray()
    while True:
     b=c.recv(8192)
     if not b:break
     data.extend(b)
    assert bytes(data)==payload
    c.sendall((b'BAD!' if bad else response));c.shutdown(socket.SHUT_WR)
  except Exception as e:errors.append(str(e))
 t=threading.Thread(target=run);t.start();return port,t

def client(port):
 with socket.create_connection((IP,port),timeout=8) as c:
  for i in range(0,len(payload),777):c.sendall(payload[i:i+777])
  c.shutdown(socket.SHUT_WR); data=bytearray()
  while True:
   b=c.recv(16384)
   if not b:break
   data.extend(b)
  assert bytes(data)==response

def command(*args,fail=False):
 r=subprocess.run([exe,*map(str,args)],capture_output=True,text=True,timeout=15)
 assert (r.returncode!=0 if fail else r.returncode==0),(args,r.stdout,r.stderr)
 return {'args':list(map(str,args)),'rc':r.returncode,'stdout':r.stdout,'stderr':r.stderr}
port,t=server();pcap=out/'tcp.pcap'
p=subprocess.Popen([exe,'record','lo',IP,str(port),str(pcap),'3'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
assert p.stdout.readline().strip()=='CAPTURING';client(port);t.join(10);assert not t.is_alive()
stdout,stderr=p.communicate(timeout=8);assert p.returncode==0,(stdout,stderr)
results=[{'record':stdout},command('inspect',pcap,IP,port)]
port2,t=server();results.append(command('replay-client',pcap,IP,port,IP,port2));t.join(10)
s=socket.socket(FAMILY);s.bind((IP,0));port3=s.getsockname()[1];s.close()
p=subprocess.Popen([exe,'replay-server',str(pcap),IP,str(port),IP,str(port3)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
assert p.stdout.readline().strip()=='LISTENING';client(port3);stdout,stderr=p.communicate(timeout=10);assert p.returncode==0,(stdout,stderr);results.append({'replay_server':stdout})
port4,t=server(True);results.append(command('replay-client',pcap,IP,port,IP,port4,fail=True));t.join(10)
# Real capture variants: duplicate retransmission, missing handshake, missing FIN,
# out-of-order data, conflicting retransmission and malformed/truncated input.
raw=pcap.read_bytes();endian='<' if raw[:4]==b'\xd4\xc3\xb2\xa1' else '>';records=[];i=24
while i<len(raw):
 n=struct.unpack_from(endian+'I',raw,i+8)[0];records.append(raw[i:i+16+n]);i+=16+n
assert i==len(raw)
def fields(r):
 b=r[16:];ip=14;v6=b[ip]>>4==6;tcp=ip+40 if v6 else ip+(b[ip]&15)*4;data=tcp+(b[tcp+12]>>4)*4
 total=40+struct.unpack_from('!H',b,ip+4)[0] if v6 else struct.unpack_from('!H',b,ip+2)[0]
 return tcp,data,total+ip-data,b[tcp+13]
idx=next(i for i,r in enumerate(records) if fields(r)[2]>0)
def variant(name,rs,fail):
 path=out/(name+'.pcap');path.write_bytes(raw[:24]+b''.join(rs));results.append(command('inspect',path,IP,port,fail=fail));return path
v=variant('duplicate',records[:idx+1]+[records[idx]]+records[idx+1:],False)
port5,t=server();results.append(command('replay-client',v,IP,port,IP,port5));t.join(10)
variant('missing-syn',records[1:],True)
variant('missing-fin',[r for r in records if not fields(r)[3]&1],True)
variant('gap',records[:idx]+records[idx+1:],True)
corrupt=bytearray(records[idx]);corrupt[16+fields(records[idx])[1]]^=1
variant('conflict',records[:idx+1]+[bytes(corrupt)]+records[idx+1:],True)
# Delay first data segment until after the next same-direction data segment.
nextidx=next(i for i in range(idx+1,len(records)) if fields(records[i])[2]>0 and records[i][16+fields(records[i])[0]:18+fields(records[i])[0]]==records[idx][16+fields(records[idx])[0]:18+fields(records[idx])[0]])
rs=records[:idx]+records[idx+1:nextidx+1]+[records[idx]]+records[nextidx+1:]
v=variant('reordered',rs,False)
port6,t=server();results.append(command('replay-client',v,IP,port,IP,port6));t.join(10)
pth=out/'truncated.pcap';pth.write_bytes(raw[:-1]);results.append(command('inspect',pth,IP,port,fail=True))
assert not errors,errors
summary={'host':socket.gethostname(),'payload_bytes':len(payload),'response_bytes':len(response),'pcap_sha256':hashlib.sha256(raw).hexdigest(),'checks':results}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
