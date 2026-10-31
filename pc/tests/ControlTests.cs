using System;
using System.Collections.Generic;
using MetalDefectMonitor;
class ControlTests
{
    static int assertions;
    static readonly DateTime T=new DateTime(2026,10,17,12,0,0,DateTimeKind.Utc);
    static void Check(bool condition,string name){assertions++;if(!condition)throw new Exception(name);}
    static string Ack(ArmControl c,string result)
    {return ArmWire.Reply("ARMCTL "+(c.Pending=="HELLO"?"HELLO":"ACK")+" session="+c.Session.ToString("X8")+" seq="+c.PendingSequence.ToString("X8")+" tick=000003E8 cmd="+c.Pending+" result="+result+" duplicate=0 proto=1");}
    static string State(ArmControl c,uint tick,int state,uint hw,int reason)
    {return ArmWire.Reply("ARMCTL STATE session="+c.Session.ToString("X8")+" seq=00000000 tick="+tick.ToString("X8")+" state="+state+" reason="+reason+" hw="+hw.ToString("X8")+" frame=50 result=1 samples=0 window=0 votes=0/0/0/0 uncertain=0 diag=0 arm5=1");}
    static bool Feed(ArmControl c,uint tick,int state,uint hw,int reason,DateTime at)
    {return c.Process(State(c,tick,state,hw,reason),at) && c.Process(Detail(c,tick),at);}
    static string Detail(ArmControl c,uint tick,string values="phase=0 flags=3 x=50 y=54 valid=1 measure=1 mode=0 cal=1 pose=4132965A claw=37")
    {return ArmWire.Reply("ARMCTL LOCSTATE session="+c.Session.ToString("X8")+" seq=00000000 tick="+tick.ToString("X8")+" "+values);}
    static ArmControl Ready(uint hw)
    {var c=new ArmControl();c.Hello(T);Check(c.Process(Ack(c,"OK"),T),"hello ack");Check(Feed(c,1000,0,hw,0,T),"initial state");return c;}
    static void Refused(ArmControl c,string verb,DateTime at,string test)
    {bool failed=false;try{c.Request(verb,at);}catch(InvalidOperationException){failed=true;}Check(failed,test);}
    static int Main()
    {
        try {
            Check(ArmWire.Crc("123456789")==0x29B1,"CRC known vector");
            Check(ArmWire.Command(1,3,1200,"START")=="@ARM1,00000001,00000003,000004B0,START,8892\n","shared C/C# command golden vector");
            Check(ArmWire.Reply("ARMCTL HELLO session=00000001 seq=00000001 tick=000003E8 cmd=HELLO result=OK duplicate=0 proto=1").EndsWith(" crc=BAC4",StringComparison.Ordinal),"shared C/C# ACK golden vector");
            Check(ArmWire.Command(1,2,1000,"START").Length<=64,"hardware FIFO frame budget");
            var old=new ArmControl();Check(!old.Allowed("START",T),"old firmware cannot start");old.Hello(T);old.Expire(T.AddSeconds(5));Check(old.NeedsHandshake && old.Pending==null && !old.Negotiated,"hello timeout no auto retry");
            Refused(old,"ENABLE",T.AddSeconds(5),"no enable without handshake");
            var c=Ready(0);Check(c.Allowed("ENABLE",T),"disabled may enable");Check(!c.Allowed("START",T),"disabled cannot start");
            string enable=c.Request("ENABLE",T);Check(enable.Contains(",ENABLE,") && !c.Enabled,"click never claims enabled");
            Refused(c,"ENABLE",T,"one in-flight command");Check(c.Process(Ack(c,"OK"),T),"enable ack");Check(!c.Allowed("ENABLE",T),"ack requires new snapshot");
            Check(Feed(c,1100,0,17,0,T.AddMilliseconds(100)),"home busy snapshot");Check(!c.Allowed("START",T.AddMilliseconds(100)) && !c.Allowed("DISABLE",T.AddMilliseconds(100)),"home busy protects controls");
            Check(Feed(c,3500,0,9,0,T.AddMilliseconds(2500)),"home ready snapshot");Check(c.Allowed("START",T.AddMilliseconds(2500)) && c.Allowed("DISABLE",T.AddMilliseconds(2500)),"ready permits sort or idle disable");
            string start=c.Request("START",T.AddMilliseconds(2500));Check(start.Contains(",START,"),"start protocol");string ack=Ack(c,"OK");Check(c.Process(ack,T.AddMilliseconds(2600)),"start ack");Check(!c.Process(ack,T.AddMilliseconds(2700)),"duplicate ack ignored");
            Check(Feed(c,3700,1,11,0,T.AddMilliseconds(2700)),"settle lock");Check(!c.Allowed("START",T.AddMilliseconds(2700)) && !c.Allowed("DISABLE",T.AddMilliseconds(2700)),"no queue or idle disable during settle");
            Check(Feed(c,4000,4,19,0,T.AddMilliseconds(3000)),"motion state");Refused(c,"DISABLE",T.AddMilliseconds(3000),"no torque removal during movement");
            Check(!Feed(c,3900,0,9,0,T.AddMilliseconds(3100)),"old state replay refused");
            Check(Feed(c,5000,0,9,0,T.AddMilliseconds(4000)),"completed state");Check(c.Allowed("START",T.AddMilliseconds(4000)),"next cycle enabled");
            Check(!c.Fresh(T.AddSeconds(8)) && !c.Allowed("DISABLE",T.AddSeconds(8)),"stale status locks controls");
            var error=Ready(9);Check(Feed(error,1200,5,11,2,T.AddMilliseconds(200)),"recognition error state");Check(error.Allowed("RETRY",T.AddMilliseconds(200)) && !error.Allowed("START",T.AddMilliseconds(200)),"explicit recognition recovery only");
            Check(Feed(error,1300,5,11,5,T.AddMilliseconds(300)),"motion fault state");Check(!error.Allowed("RETRY",T.AddMilliseconds(300)),"motion fault not remotely cleared");
            var timeout=Ready(9);timeout.Request("START",T);timeout.Expire(T.AddSeconds(5));Check(timeout.NeedsHandshake && timeout.Pending==null && !timeout.Allowed("START",T.AddSeconds(5)),"start timeout unknown outcome no auto repeat");
            timeout.Hello(T.AddSeconds(6));Check(timeout.Pending=="HELLO" && timeout.Enabled,"manual hello no hardware mutation");
            var corrupt=Ready(9);string valid=State(corrupt,1200,4,19,0);Check(!corrupt.Process(valid.Replace("state=4","state=0"),T),"CRC corruption rejected");
            Check(!corrupt.Process(ArmWire.Reply("ARMCTL STATE session=12345678 seq=00000000 tick=000004B0 state=0 reason=0 hw=00000009 frame=1 result=0 samples=0 window=0 votes=0/0/0/0 uncertain=0 diag=0"),T),"wrong connection session refused");
            Check(!Feed(corrupt,1200,99,9,0,T),"invalid state rejected");
            Check(corrupt.Process(ArmWire.Reply("ARMCTL READY proto=1 firmware=20261017 pwm_default=0"),T),"board boot signal");Check(!corrupt.Negotiated && !corrupt.Allowed("START",T),"boot invalidates control session");
            var gap=Ready(9);gap.Disconnect();Check(!gap.Allowed("DISABLE",T) && gap.Pending==null,"disconnect does not send disable");
            string type;Dictionary<string,string> fields;Check(!ArmWire.Parse("ARMCTL ACK result=OK",out type,out fields),"no CRC no control ack");
            var votes=Ready(9);string inconsistent=State(votes,1200,0,9,0).Replace("votes=0/0/0/0","votes=5/0/0/0");int end=inconsistent.IndexOf(" crc=",StringComparison.Ordinal);inconsistent=ArmWire.Reply(inconsistent.Substring(0,end));Check(!votes.Process(inconsistent,T),"vote/window consistency");
            var arm5=Ready(9);
            Check(Feed(arm5,1200,5,11,14,T.AddMilliseconds(200)),"ARM5 locator error accepted");
            Check(arm5.Allowed("RETRY",T.AddMilliseconds(200)),"ARM5 locator explicit recovery");
            Check(Feed(arm5,1300,5,11,17,T.AddMilliseconds(300)),"ARM5 motion timeout visible");
            Check(!arm5.Allowed("RETRY",T.AddMilliseconds(300)),"ARM5 motion timeout requires inspection");
            Check(!Feed(arm5,1400,5,11,26,T.AddMilliseconds(400)),"undefined ARM5 reason rejected");

            foreach(string verb in new[]{"TEACH","LOCATE"})Check(ArmWire.Command(1,2,1000,verb).Length<=64,"FIFO budget "+verb);
            var teach=Ready(9);
            Check(teach.Process(State(teach,1100,0,9,0),T),"teach preparation state");
            Check(teach.Process(Detail(teach,1100,"phase=0 flags=2 x=0 y=0 valid=0 measure=0 mode=0 cal=1 pose=4132965A claw=37"),T),"observation not taught");
            Check(teach.Allowed("TEACH",T),"ready open claw may record observation");
            Refused(teach,"START",T,"no full start before teach");Refused(teach,"LOCATE",T,"no locate before teach");
            teach.Request("TEACH",T);Check(teach.Process(Ack(teach,"OK"),T),"teach ack");
            Check(!teach.Allowed("LOCATE",T),"teach ack is not observation readback");
            Check(teach.Process(State(teach,1200,0,9,0),T.AddMilliseconds(200)),"teach updated state");
            Check(!teach.Allowed("START",T.AddMilliseconds(200)),"STATE alone cannot authorize workflow");
            Check(!teach.Process(Detail(teach,1100),T.AddMilliseconds(200)),"wrong-tick detail refused");
            Check(teach.Process(Detail(teach,1200),T.AddMilliseconds(200)),"matching observation readback");
            Check(teach.ObservationTaught && teach.Allowed("START",T.AddMilliseconds(200)) && teach.Allowed("LOCATE",T.AddMilliseconds(200)),"observation readback opens workflow");
            Check(!teach.Process(Detail(teach,1200),T),"duplicate detail refused");
            Check(teach.Process(State(teach,1300,1,11,0),T.AddMilliseconds(300)),"new snapshot invalidates old coordinates");
            Check(!teach.CoordinatesValid && !teach.WorkflowFresh(T.AddMilliseconds(300)),"no old coordinates between paired states");
            Check(teach.Process(Detail(teach,1300,"phase=3 flags=3 x=50 y=54 valid=0 measure=2 mode=1 cal=1 pose=4132965A claw=37"),T.AddMilliseconds(300)),"locate settle detail");
            Check(teach.Phase==3 && teach.LocateOnly && !teach.CoordinatesValid,"mode and phase shown");
            Check(!teach.Allowed("TEACH",T.AddMilliseconds(300)) && !teach.Allowed("LOCATE",T.AddMilliseconds(300)),"busy cycle rejects teach and repeated locate");
            Check(teach.Process(State(teach,3000,0,9,0),T.AddSeconds(2)),"locate completed state");
            string detail=Detail(teach,3000,"phase=0 flags=3 x=50 y=54 valid=1 measure=2 mode=1 cal=1 pose=4132965A claw=37");
            Check(!teach.Process(detail.Replace("x=50","x=99"),T.AddSeconds(2)),"coordinate CRC required");
            Check(!teach.Process(ArmWire.Reply(detail.Substring(0,detail.IndexOf(" crc=",StringComparison.Ordinal)).Replace("x=50","x=101")),T.AddSeconds(2)),"out-of-frame coordinate refused");
            Check(teach.Process(detail,T.AddSeconds(2)),"integer coordinates accepted");
            Check(teach.Xmm==50 && teach.Ymm==54 && teach.Measure==2 && teach.CoordinatesValid && teach.LocateOnly,"locate-only result bound to exact snapshot");
            Check(teach.Allowed("START",T.AddSeconds(2)) && teach.Allowed("DISABLE",T.AddSeconds(2)),"locate completion waits for explicit next command");
            var legacy=Ready(9);
            string legacyState=State(legacy,1200,0,9,0);legacyState=ArmWire.Reply(legacyState.Substring(0,legacyState.IndexOf(" crc=",StringComparison.Ordinal)).Replace(" arm5=1",""));
            Check(legacy.Process(legacyState,T),"legacy firmware readable");
            Check(!legacy.Allowed("START",T) && !legacy.Allowed("TEACH",T) && !legacy.Allowed("LOCATE",T),"legacy snapshot locks new workflow");
            Check(!legacy.Process(Detail(legacy,1200),T),"detail without ARM5 capability refused");
            var late=Ready(9);Check(late.Process(State(late,1200,0,9,0),T),"late detail base");
            Check(late.Process(Detail(late,1200),T.AddSeconds(4)),"delayed detail parses");
            Check(!late.WorkflowFresh(T.AddSeconds(4)) && !late.Allowed("START",T.AddSeconds(4)),"detail does not refresh expired STATE lease");
            var closed=Ready(9);Check(closed.Process(State(closed,1200,0,9,0),T),"closed claw state");
            Check(closed.Process(Detail(closed,1200,"phase=0 flags=2 x=0 y=0 valid=0 measure=0 mode=0 cal=1 pose=4132965A claw=60"),T),"closed claw detail");
            Check(!closed.Allowed("TEACH",T),"claw must be open before teach");
            var bounds=Ready(9);Check(bounds.Process(State(bounds,1200,0,9,0),T),"detail bounds base");
            string bounded=Detail(bounds,1200);string boundedBody=bounded.Substring(0,bounded.IndexOf(" crc=",StringComparison.Ordinal));
            foreach(string[] pair in new[]{new[]{"phase=0","phase=8"},new[]{"flags=3","flags=4"},new[]{"x=50","x=-1"},new[]{"y=54","y=101"},new[]{"valid=1","valid=2"},new[]{"mode=0","mode=2"},new[]{"cal=1","cal=2"},new[]{"pose=4132965A","pose=FF32965A"},new[]{"claw=37","claw=0"}})
                Check(!bounds.Process(ArmWire.Reply(boundedBody.Replace(pair[0],pair[1])),T),"invalid workflow field "+pair[0]);
            Check(!bounds.Process(Detail(bounds,1300),T),"future-tick detail refused");
            Check(!bounds.Process(ArmWire.Reply(boundedBody.Replace("session="+bounds.Session.ToString("X8"),"session=00000000")),T),"foreign workflow session refused");
            Check(bounds.Process(Detail(bounds,1200,"phase=4 flags=3 x=50 y=54 valid=1 measure=1 mode=0 cal=1 pose=4132965A claw=37"),T),"phase inconsistent with idle received");
            Check(!bounds.Allowed("START",T) && !bounds.Allowed("TEACH",T),"non-idle detailed phase blocks motion and teach");
            Console.WriteLine("CONTROL_TESTS_PASS assertions="+assertions);return 0;
        } catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
    }
}
