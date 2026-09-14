import zipfile, numpy as np, json, struct, sys
ARQ=sys.argv[1] if len(sys.argv)>1 else "../captures/pulso_limpo2.sr"
SR=4_000_000
tab=[]
for i in range(256):
    c=i<<8
    for _ in range(8): c=((c<<1)^0x1021)&0xFFFF if c&0x8000 else (c<<1)&0xFFFF
    tab.append(c)
def crc16(b):
    c=0xFFFF
    for x in b: c=((c<<8)^tab[((c>>8)^x)&0xFF])&0xFFFF
    return c
z=zipfile.ZipFile(ARQ)
bl=sorted([n for n in z.namelist() if n.startswith("logic-1-")],key=lambda n:int(n.rsplit("-",1)[1]))
d=np.frombuffer(b"".join(z.read(n) for n in bl),dtype=np.uint8)
C=lambda c:((d>>c)&1).astype(np.uint8)
E=lambda s:(np.flatnonzero(np.diff(s.astype(np.int8)))+1)
sck,miso,mosi,nss=[C(x) for x in (2,1,0,3)]
ris=np.flatnonzero((sck[1:]==1)&(sck[:-1]==0))+1
i3=E(nss); desc=i3[nss[i3]==0]; sub=i3[nss[i3]==1]
k=np.searchsorted(sub,desc); ok=k<len(sub); desc,fim=desc[ok],sub[k[ok]]
def bytes_janela(sig,a,b):
    idx=ris[(ris>=a)&(ris<b)]
    n=len(idx)//8
    v=sig[idx[:n*8]].reshape(n,8)
    vals=np.packbits(v,axis=1,bitorder='big').ravel()
    lim=[(float(idx[8*i]),float(idx[8*i+7])) for i in range(n)]
    return vals,lim
def campos(buf):
    for off in (0,1,2):
        if off+7<=len(buf) and buf[off]==0xAA and buf[off+1]==0x55:
            ln=int(buf[off+5])|(int(buf[off+6])<<8); tot=7+ln+2
            if off+tot>len(buf): return None
            pkt=buf[off:off+tot]
            calc=crc16(pkt[:-2]); be=(int(pkt[-2])<<8)|int(pkt[-1]); le=int(pkt[-2])|(int(pkt[-1])<<8)
            if calc not in (be,le): return None
            f=[("SOF",off,off+1),("DST",off+2,off+2),("SRC",off+3,off+3),("CMD",off+4,off+4),("LEN",off+5,off+6)]
            if ln: f.append(("PAYLOAD",off+7,off+6+ln))
            f.append(("CRC",off+7+ln,off+8+ln))
            return {"offset":off,"len":ln,"cmd":int(pkt[4+off]),"campos":f,"total":tot,"pkt":bytes(pkt)}
    return None
# Escolhe um par pedido/resposta REPRESENTATIVO: as duas transferencias com
# duracao proxima da mediana do arquivo, para a figura nao mostrar um caso
# atipico (a cauda vai de 1.161 a 1.913 us conforme a ocupacao do mestre).
dur=(fim-desc)/ (SR/1e6)
med=float(np.median(dur))
par=None; melhor=None
for i in range(len(desc)-1):
    if abs(dur[i]-med)>0.02*med or abs(dur[i+1]-med)>0.02*med: continue
    bq,_=bytes_janela(mosi,desc[i],fim[i]); fq=campos(bq)
    if not fq or fq["cmd"]!=0x03: continue
    bm,_=bytes_janela(miso,desc[i+1],fim[i+1]); fm=campos(bm)
    if fm and fm["cmd"]==0x04 and fm["len"]==14:
        st,vol,rate,pres,al=struct.unpack("<BIIIB",fm["pkt"][7:21])
        if rate>=600 and st==2:
            par=(i,i+1,fq,fm,(st,vol,rate,pres,al))
            print("mediana do arquivo %.1f us; par escolhido %.1f / %.1f us"%(med,dur[i],dur[i+1]))
            break
if par is None: print("nenhum par com RUNNING/600 encontrado"); sys.exit(1)
i_req,i_res,f_req,f_res,dec=par
print("payload:",dec)
NOMES={0x03:"GET_STATUS request",0x04:"GET_STATUS reply"}
t0=int(desc[i_req]-80e-6*SR); t1=int(fim[i_res]+140e-6*SR)
out={"arquivo":ARQ,"janela_us":[0.0,(t1-t0)/SR*1e6],
     "transferencias":[{"ini":(desc[i]-t0)/SR*1e6,"fim":(fim[i]-t0)/SR*1e6} for i in (i_req,i_res)],
     "canais":{},"quadros":[],
     "cadencia_us":{"clock":round(float(np.median([b[1]-b[0] for b in bytes_janela(mosi,desc[i_req],fim[i_req])[1]]))/(SR/1e6),2),
      "pausa":round(float(np.median(np.diff([b[0] for b in bytes_janela(mosi,desc[i_req],fim[i_req])[1]])))/(SR/1e6)-float(np.median([b[1]-b[0] for b in bytes_janela(mosi,desc[i_req],fim[i_req])[1]]))/(SR/1e6),2)},
     "estado":{"state":dec[0],"volume":dec[1],"rate":dec[2],"pressure":dec[3],"alarm":dec[4]}}
for nome,ch in [("NSS",3),("SCK",2),("MOSI",0),("MISO",1),("READY",4),("MARK_A",6)]:
    s=C(ch); tr=E(s); tr=tr[(tr>=t0)&(tr<=t1)]
    out["canais"][nome]={"nivel_inicial":int(s[t0]),
                         "transicoes_us":[round(float((x-t0)/SR*1e6),3) for x in tr]}
for (idx,f,sig,linha) in ((i_req,f_req,mosi,"MOSI"),(i_res,f_res,miso,"MISO")):
    vals,lim=bytes_janela(sig,desc[idx],fim[idx])
    n=f["total"]+f["offset"]
    out["quadros"].append({"linha":linha,"cmd":f["cmd"],"nome":NOMES.get(f["cmd"],f"0x{f['cmd']:02X}"),
      "len":f["len"],
      "bytes":[{"hex":f"{int(vals[j]):02X}","t0":round((lim[j][0]-t0)/SR*1e6,3),
                "t1":round((lim[j][1]-t0)/SR*1e6,3)} for j in range(min(n+2,len(vals)))],
      "campos":[{"nome":c[0],"de":c[1],"ate":c[2]} for c in f["campos"]]})
json.dump(out,open("dados/f3.json","w"))
print("janela %.0f us"%out["janela_us"][1], out["transferencias"], "marker A:",out["canais"]["MARK_A"]["transicoes_us"])
print("req:"," ".join(b["hex"] for b in out["quadros"][0]["bytes"]))
print("res:"," ".join(b["hex"] for b in out["quadros"][1]["bytes"]))
