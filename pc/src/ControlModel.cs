using System;
using System.Collections.Generic;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;

namespace MetalDefectMonitor
{
    public static class ArmWire
    {
        public static ushort Crc(string text)
        {
            ushort crc=0xffff;
            foreach(char c in text) { crc^=(ushort)(c<<8); for(int i=0;i<8;i++)crc=(ushort)((crc&0x8000)!=0?(crc<<1)^0x1021:crc<<1); }
            return crc;
        }
        public static string Command(uint session,uint seq,uint stamp,string verb)
        {
            string body="ARM1,"+session.ToString("X8")+","+seq.ToString("X8")+","+stamp.ToString("X8")+","+verb;
            return "@"+body+","+Crc(body).ToString("X4")+"\n";
        }
        public static string Reply(string body) { return body+" crc="+Crc(body).ToString("X4"); }
        public static bool Parse(string line,out string type,out Dictionary<string,string> fields)
        {
            type=null;fields=new Dictionary<string,string>(StringComparer.Ordinal);
            if(line==null || line.Length>320 || !line.StartsWith("ARMCTL ",StringComparison.Ordinal))return false;
            int end=line.LastIndexOf(" crc=",StringComparison.Ordinal);ushort crc;
            if(end<0 || line.Length-end!=9 || !UInt16.TryParse(line.Substring(end+5),NumberStyles.HexNumber,CultureInfo.InvariantCulture,out crc) || Crc(line.Substring(0,end))!=crc)return false;
            string[] parts=line.Substring(0,end).Split(' ');if(parts.Length<3)return false;
            type=parts[1];
            for(int i=2;i<parts.Length;i++) {int eq=parts[i].IndexOf('=');if(eq<1 || eq==parts[i].Length-1 || fields.ContainsKey(parts[i].Substring(0,eq)))return false;fields.Add(parts[i].Substring(0,eq),parts[i].Substring(eq+1));}
            return true;
        }
        public static bool Number(Dictionary<string,string> fields,string key,bool hex,out uint n)
        { string s;n=0;return fields.TryGetValue(key,out s) && UInt32.TryParse(s,hex?NumberStyles.HexNumber:NumberStyles.None,CultureInfo.InvariantCulture,out n); }
    }

    // Decisions are based on CRC-checked fresh board snapshots, never button clicks.
    // One request in flight; timeouts invalidate the session and are never retried.
    public sealed class ArmControl
    {
        public uint Session,Sequence,PendingSequence,Tick,Hardware,Frame,Samples,Window,Uncertain;
        public readonly uint[] Votes=new uint[4];
        public int State,Reason,Result;
        public uint Phase,Flags,Xmm,Ymm,Measure,WorkflowTick,Pose,Claw;
        public bool Arm5,HasWorkflow,CoordinatesValid,LocateOnly,Calibrated;
        public bool Negotiated,HasState,NeedsHandshake;
        public string Pending,Message="未建立控制会话；旧版 ELF 仍可只读监控。";
        public DateTime LastState,Sent;
        bool waitingState;
        public bool Enabled {get{return (Hardware&1)!=0;}}
        public bool Ready {get{return (Hardware&8)!=0;}}
        public bool Busy {get{return (Hardware&16)!=0;}}
        public bool Locked {get{return (Hardware&6)!=0;}}
        public bool Fresh(DateTime now){return Negotiated && !NeedsHandshake && HasState && now>=LastState && (now-LastState).TotalSeconds<=3;}
        public bool WorkflowFresh(DateTime now){return Fresh(now) && Arm5 && HasWorkflow && WorkflowTick==Tick;}
        public bool ObservationTaught {get{return (Flags&1)!=0;}}
        public string PhaseName {get{
            string[] names={"空闲 / 等待操作","回 HOME / 手动调整","返回观察位","稳定画面 / 圆片定位","移动到圆片上方","缺陷识别 / 五帧投票","抓取 / 分类投放 / 返回","流程故障"};
            return Phase<8?names[Phase]:"阶段未知";
        }}
        public bool Allowed(string verb,DateTime now)
        {
            if(!WorkflowFresh(now) || Pending!=null || waitingState || Hardware==UInt32.MaxValue || Busy)return false;
            if(verb=="RETRY")return Enabled && Ready && State==5 && ((Reason>=1 && Reason<=3) || Reason==12 || Reason==14 || Reason==15 || (Reason>=21 && Reason<=23));
            if(State!=0 || Phase!=0 || Locked)return false;
            if(verb=="ENABLE")return !Enabled;
            if(verb=="TEACH")return Enabled && Ready && WorkflowFresh(now) && Calibrated && (Flags&2)!=0 && Claw==37;
            if(verb=="START" || verb=="LOCATE")return Enabled && Ready && WorkflowFresh(now) && Calibrated && (Flags&3)==3;
            if(verb=="DISABLE")return Enabled;
            return false;
        }
        public void Disconnect()
        {Pending=null;Negotiated=false;HasState=false;HasWorkflow=false;Arm5=false;CoordinatesValid=false;NeedsHandshake=true;waitingState=false;Message="连接已断开，未发送失能或停止命令；板端已开始的动作可能继续。";}
        public string Hello(DateTime now)
        {
            if(Pending!=null)throw new InvalidOperationException("上一条指令还在等待确认。");
            byte[] bytes=new byte[4];uint nonce;
            using(var rng=RandomNumberGenerator.Create()){do{rng.GetBytes(bytes);nonce=BitConverter.ToUInt32(bytes,0);}while(nonce==0 || nonce==Session);}
            Session=nonce;Sequence=0;Negotiated=false;HasState=false;HasWorkflow=false;Arm5=false;CoordinatesValid=false;NeedsHandshake=false;waitingState=false;
            Message="正在握手（不会使能、复位或触发动作）…";
            return Begin("HELLO",0,now);
        }
        string Begin(string verb,uint stamp,DateTime now)
        {
            if(Sequence==UInt32.MaxValue)throw new InvalidOperationException("序号耗尽，请重新握手。");
            Pending=verb;PendingSequence=++Sequence;Sent=now;
            return ArmWire.Command(Session,PendingSequence,stamp,verb);
        }
        public string Request(string verb,DateTime now)
        {
            if(!Allowed(verb,now))throw new InvalidOperationException("状态不允许此操作；请等待新状态或重新握手。动作期间不能失能。 ");
            Message="等待开发板确认："+verb+"（不会自动重发）";
            return Begin(verb,Tick,now);
        }
        public void Expire(DateTime now)
        {
            if((Pending!=null || waitingState) && (now-Sent).TotalSeconds>4)
            {string old=Pending??"状态回读";Pending=null;waitingState=false;NeedsHandshake=true;Negotiated=false;HasState=false;HasWorkflow=false;CoordinatesValid=false;
             Message=old=="HELLO"?"握手未完成，当前仅监控；确认已加载配套UART ELF且CPU运行，再点重新握手。没有发送动作命令。":old+" 确认超时：执行结果未知，未重发。先观察机械臂，点重新握手获取现状。";}
        }
        public bool Process(string line,DateTime at)
        {
            string type;Dictionary<string,string> f;
            if(!ArmWire.Parse(line,out type,out f))return false;
            if(type=="READY") { Disconnect();Message="收到新程序启动标记，请重新握手；机械臂默认失能。";return true; }
            uint session,seq,tick;
            if(!ArmWire.Number(f,"session",true,out session) || session!=Session || !ArmWire.Number(f,"seq",true,out seq) || !ArmWire.Number(f,"tick",true,out tick))return false;
            if(type=="HELLO" || type=="ACK")
            {
                string cmd,result;uint proto,duplicate;
                if(Pending==null || seq!=PendingSequence || !f.TryGetValue("cmd",out cmd) || cmd!=Pending || !f.TryGetValue("result",out result) || !ArmWire.Number(f,"proto",false,out proto) || proto!=1 || !ArmWire.Number(f,"duplicate",false,out duplicate) || duplicate>1)return false;
                bool hello=Pending=="HELLO";
                if((hello && type!="HELLO") || (!hello && type!="ACK"))return false;
                Pending=null;
                if(hello) {Negotiated=result=="OK";NeedsHandshake=!Negotiated;HasState=false;}
                if(result=="OK" || result=="ALREADY_ENABLED" || result=="ALREADY_DISABLED")
                {waitingState=true;Sent=at;Message=hello?"控制握手成功，等待板端状态。":"开发板已确认 "+cmd+"；等待状态回读，不代表动作完成。";}
                else {waitingState=false;Message="开发板拒绝 "+cmd+"："+Translate(result);}
                return true;
            }
            if(type=="LOCSTATE") {
                uint phase,flags,x,y,valid,measure,mode,cal,pose,claw;
                if(!Negotiated || NeedsHandshake || !HasState || !Arm5 || seq!=0 || tick!=Tick || HasWorkflow ||
                   !ArmWire.Number(f,"phase",false,out phase) || phase>7 || !ArmWire.Number(f,"flags",false,out flags) || flags>3 ||
                   !ArmWire.Number(f,"x",false,out x) || x>100 || !ArmWire.Number(f,"y",false,out y) || y>100 ||
                   !ArmWire.Number(f,"valid",false,out valid) || valid>1 || !ArmWire.Number(f,"measure",false,out measure) ||
                   !ArmWire.Number(f,"mode",false,out mode) || mode>1 || !ArmWire.Number(f,"cal",false,out cal) || cal>1 ||
                   !ArmWire.Number(f,"pose",true,out pose) || !ArmWire.Number(f,"claw",false,out claw) || claw<37 || claw>90)return false;
                for(int i=0;i<4;i++)if(((pose>>(8*i))&255)>180)return false;
                Phase=phase;Flags=flags;Xmm=x;Ymm=y;CoordinatesValid=valid==1;Measure=measure;LocateOnly=mode==1;Calibrated=cal==1;
                Pose=pose;Claw=claw;WorkflowTick=tick;HasWorkflow=true;return true;
            }
            if(type!="STATE" || !Negotiated || NeedsHandshake || seq!=0)return false;
            uint state,reason,resultId,hw,frame,samples,window,uncertain,diag;string votes;
            if(!ArmWire.Number(f,"state",false,out state) || state>5 || !ArmWire.Number(f,"reason",false,out reason) || reason>25 || (reason>5 && reason<10) || reason==19 || reason==20 || !ArmWire.Number(f,"result",false,out resultId) || resultId>3 ||
                !ArmWire.Number(f,"hw",true,out hw) || !ArmWire.Number(f,"frame",false,out frame) || !ArmWire.Number(f,"samples",false,out samples) || samples>1000000 ||
                !ArmWire.Number(f,"window",false,out window) || window>5 || !ArmWire.Number(f,"uncertain",false,out uncertain) || uncertain>5 || !ArmWire.Number(f,"diag",false,out diag) || !f.TryGetValue("votes",out votes))return false;
            string[] v=votes.Split('/');uint[] parsed=new uint[4];uint total=uncertain;
            if(v.Length!=4)return false;
            for(int i=0;i<4;i++){if(!UInt32.TryParse(v[i],NumberStyles.None,CultureInfo.InvariantCulture,out parsed[i]) || parsed[i]>5)return false;total+=parsed[i];}
            if(total!=window || (HasState && unchecked((int)(tick-Tick))<=0))return false;
            uint capability=0;
            if(f.ContainsKey("arm5") && (!ArmWire.Number(f,"arm5",false,out capability) || capability!=1))return false;
            bool wasWaiting=waitingState;
            State=(int)state;Reason=(int)reason;Result=(int)resultId;Hardware=hw;Frame=frame;Samples=samples;Window=window;Uncertain=uncertain;Array.Copy(parsed,Votes,4);Tick=tick;LastState=at;HasState=true;waitingState=false;
            Arm5=capability==1;HasWorkflow=false;CoordinatesValid=false;
            if(wasWaiting)Message="板端状态已回读；只接受单次指令，实际阶段以上方状态为准。";
            if(diag!=0){NeedsHandshake=true;Message="板端诊断故障 "+diag+"；拒绝控制，请检查 XSCT。";}
            return true;
        }
        static string Translate(string value)
        {
            switch(value){
                case "BUSY":return "机械臂忙碌";case "NOT_IDLE":return "识别或分拣尚未结束";
                case "NOT_READY":return "未使能或尚未回到初始状态";case "STALE_LEASE":return "状态凭据已过期（可能 CPU 暂停），没有执行";
                case "INSPECT_ARM_FIRST":return "动作故障，需先检查机械臂";case "BAD_DIAG":return "板端诊断异常";
                case "OBSERVE_NOT_TAUGHT":return "尚未记录观察位，请先点击“记录当前观察位”";
                case "CLAW_NOT_OPEN":return "夹爪未处于打开角度，不能记录观察位";
                case "OBSERVE_TEACH_FAILED":return "观察位记录失败，请检查当前阶段与夹爪状态";
                case "BAD_CALIBRATION":return "五舵机标定数据无效，请检查整合工程";
                case "HOME_FAILED":return "回初始位置指令失败，请检查机械臂";
                default:return value;
            }
        }
    }
}
