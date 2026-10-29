"""Read public n64decomp/007 setup source as data for converter/composer tests
(from PR #224; test_arena.py runs the seven "(Full)" maps through gevr_arena.c).

No setup assets are stored here. AST evaluation accepts only numeric literals,
negation and the two word-packing macros; reference source is never executed.
AI/path code is omitted, and floor queries in the runner are stubs.
"""
import ast,re,struct
from pathlib import Path
def parts(s):
 result=[];d=0;start=0
 for i,c in enumerate(s):
  d+=(c=='(')-(c==')')
  if c==',' and not d:
   if s[start:i].strip():result.append(s[start:i].strip())
   start=i+1
 if s[start:].strip():result.append(s[start:].strip())
 return result
def expr(s):
 def ev(n):
  if isinstance(n,ast.Constant):return n.value
  if isinstance(n,ast.UnaryOp) and isinstance(n.op,ast.USub):return -ev(n.operand)
  if isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id in ['_mkword','_mkshort']:
   a,b=map(ev,n.args);bits=16 if n.func.id=='_mkword' else 8
   return ((a&((1<<bits)-1))<<bits)|(b&((1<<bits)-1))
  raise ValueError(ast.dump(n))
 return ev(ast.parse(re.sub(r'(\d(?:\.\d*)?(?:e[+-]?\d+)?)f\b',r'\1',s,flags=re.I),mode='eval').body)
def blob(path):
 s=path.read_text();buf=bytearray(40);offsets=[0]*10
 for name,idx in [('intro',2),('propDefs',3)]:
  m=re.search(r's32 '+name+r'\[\] = \{(.*?)\n\};',s,re.S)
  st=re.sub(r'/\*.*?\*/','',m[1],flags=re.S)
  vals=[expr(x) for x in parts(st)]
  offsets[idx]=len(buf)
  for v in vals:buf.extend(struct.pack('>f',v) if isinstance(v,float) else struct.pack('>I',v&0xffffffff))
 names=[]
 for name,idx,bound in [('padlist',6,False),('pad3dlist',7,True)]:
  m=re.search(r'(?:Bound)?PadRecord '+name+r'\[\] = \{(.*?)\n\};',s,re.S);offsets[idx]=len(buf)
  for line in m[1].strip().splitlines():
   line=line.strip()
   if not line:continue
   link=re.search(r'"([^"]*)"',line) # "" is a name (Statue's pad 7), NULL ends the list
   if not link:buf.extend(bytes(68 if bound else 44));continue
   vals=[float(x) for x in re.findall(r'[-+]?(?:\d+\.\d*|\d+)(?:e[-+]?\d+)?',line.split('"')[0],flags=re.I)]
   if len(vals)!=9:raise ValueError(line)
   start=len(buf);buf.extend(struct.pack('>9fII',*vals,0,0));names.append((start+36,link[1]))
   if bound:
    nums=[float(x) for x in re.findall(r'[-+]?(?:\d+\.\d*|\d+)(?:e[-+]?\d+)?',line.split('"')[2],flags=re.I)]
    buf.extend(struct.pack('>6f',*nums[-6:]))
 for at,name in names:
  struct.pack_into('>I',buf,at,len(buf));buf.extend(name.encode()+b'\0')
 struct.pack_into('>10I',buf,0,*offsets)
 return buf
def build_records(decomp_root, output):
    root=Path(decomp_root)/"assets/obseg/setup"
    output=Path(output)
    (output/"mp.bin").write_bytes(blob(root/"Ump_setuparkZ.c"))
    paths=[]
    for name in ["ark","sevb","arch","cave","cryp","statue","crad"]:
        path=next(p for p in [root/f"Usetup{name}Z.c",root/"u"/f"Usetup{name}Z.c"] if p.exists())
        target=output/f"{name}.bin"
        target.write_bytes(blob(path))
        paths.append(target)
    return paths,output/"mp.bin"
