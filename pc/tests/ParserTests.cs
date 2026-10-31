using System;
using System.IO;
using MetalDefectMonitor;
class ParserTests
{
    static int assertions;
    static readonly DateTime T = new DateTime(2026,10,17,12,0,0);
    static void Check(bool condition, string name) { assertions++; if (!condition) throw new Exception(name); }
    static string Arm(int state, int cls, uint frame) { return "ARM_SORT state="+state+" result="+cls+" samples="+(state>=3 && state<=4 || state==0?5:0)+" frame="+frame+" reason=0 mixed=0"; }
    static void Complete(MonitorModel m,int cls,uint start)
    {
        int[] states={1,2,3,4,0};
        for(int i=0;i<states.Length;i++) m.Process(Arm(states[i],cls,unchecked(start+(uint)i)),T.AddSeconds(i*2));
    }
    static int Main()
    {
        try {
            var m=new MonitorModel("测试");
            for(int c=0;c<4;c++){Complete(m,c,(uint)(10+c*10));Check(m.Counts[c]==1,"four classes");}
            Check(m.Completed==4,"one count per full cycle");
            m.Process(Arm(0,3,44),T);m.Process(Arm(0,3,44),T);Check(m.Completed==4,"duplicate completion");
            Check(m.Records[0].Seconds==8,"host observed elapsed");
            var partial=new MonitorModel("测试");partial.Process(Arm(4,1,12),T);partial.Process(Arm(0,1,13),T);Check(partial.Completed==0,"mid-motion connection not counted");
            partial.Process(Arm(0,0,14),T);Check(partial.Counts[0]==0,"default result zero is not normal detection");
            partial.Process(Arm(1,1,20),T);partial.Process(Arm(2,1,21),T);partial.BreakConnection("test");partial.Process(Arm(3,1,22),T);partial.Process(Arm(4,1,23),T);partial.Process(Arm(0,1,24),T);Check(partial.Completed==0,"gap invalidates cycle");
            Complete(partial,1,30);Check(partial.Completed==1,"recover next full cycle");
            var e=new MonitorModel("测试");e.Process(Arm(1,0,1),T);e.Process("ARM_SORT state=5 result=0 samples=7 frame=3 reason=2 mixed=6",T);e.Process("ARM_SORT state=5 result=0 samples=7 frame=3 reason=2 mixed=6",T);Check(e.Errors==1 && e.Completed==0,"error counted once, never normal");
            Check(!e.Process("ARM_SORT state=99 result=0 samples=5 frame=4 reason=0 mixed=0",T),"invalid state");
            Check(!e.Process("ARM_SORT state=1 result=999 samples=5 frame=4 reason=0 mixed=0",T),"invalid class");
            Check(!e.Process("ARM_SORT state=1 result=0 samples=5 frame=999999999999999 reason=0 mixed=0",T),"overflow");
            var bad=new MonitorModel("测试");bad.Process(Arm(1,1,1),T);bad.Process(Arm(2,1,2),T);bad.Process(Arm(3,1,3),T);bad.Process(Arm(4,2,4),T);bad.Process(Arm(0,2,5),T);Check(bad.Completed==0,"class mismatch");
            var wrap=new MonitorModel("测试");Complete(wrap,2,UInt32.MaxValue-2);Check(wrap.Completed==1,"uint32 frame rollover");
            var reset=new MonitorModel("测试");reset.Process(Arm(1,1,100),T);reset.Process("YOLO accelerator started",T);reset.Process(Arm(4,1,2),T);reset.Process(Arm(0,1,3),T);Check(reset.Completed==0,"boot resets observation");
            reset.Process("YOLO inference: 295488 us",T);Check(reset.InferenceMs==295.488,"inference not whole-frame fps");
            reset.Process("DEFECT frame=30 id=2 class=dent score_ppm=998259 box=420,529,479,564",T);Check(reset.Detections.Count==1 && reset.Detections[0].ClassId==2,"dent ID mapping");
            reset.Process("DEFECT frame=30 id=2 class=dent score_ppm=998259 box=420,529,479,564",T);Check(reset.Detections.Count==1,"detection duplicate no extra count");
            Check(reset.Completed==0,"detections are not workpiece counts");
            var framer=new LineFramer();Check(framer.Push("ARM_SO").Count==0,"partial line");Check(framer.Push("RT a\r\nB\n").Count==2,"chunk boundaries and crlf");
            Check(framer.Push(new string('x',9000)+"\nGOOD\n").Count==1 && framer.OversizeLines==1,"bounded lines");
            var replay=new MonitorModel("回放");int[] order={1,2,3,4,0};for(int i=0;i<5;i++)replay.Process(Arm(order[i],3,(uint)i),null);Check(replay.Completed==1 && !replay.Records[0].Seconds.HasValue,"no invented replay duration");
            Check(replay.ExportCsv().Contains("无时间戳") && replay.ExportCsv().Contains("不等于已传感"),"CSV honest metadata");
            var corrupt=new MonitorModel("测试");corrupt.Process(Arm(1,1,1),T);corrupt.Process(Arm(2,1,2),T);
            corrupt.Process("ARM_SORT state=bad",T);corrupt.Process(Arm(3,1,3),T);corrupt.Process(Arm(4,1,4),T);corrupt.Process(Arm(0,1,5),T);
            Check(corrupt.Completed==0,"malformed state invalidates observation");
            var missing=new MonitorModel("测试");missing.Process(Arm(1,1,1),T);missing.Process(Arm(3,1,3),T);missing.Process(Arm(4,1,4),T);missing.Process(Arm(0,1,5),T);
            Check(missing.Completed==0,"missing observe never counted");
            var reverse=new MonitorModel("测试");reverse.Process(Arm(1,1,100),T);reverse.Process(Arm(2,1,90),T);reverse.Process(Arm(3,1,91),T);reverse.Process(Arm(4,1,92),T);reverse.Process(Arm(0,1,93),T);
            Check(reverse.Completed==0,"reversed frame rejected");
            var danger=new MonitorModel("=not-a-formula");Complete(danger,0,1);Check(danger.ExportCsv().Contains("'=not-a-formula"),"CSV formula-prefix quoting");
            var fixture=new MonitorModel("回放测试");
            foreach(string raw in File.ReadLines(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"演示回放_非实测.log"))) {
                string line;DateTime? at;MonitorModel.UnpackRecordedLine(raw,out line,out at);fixture.Process(line,at);
            }
            Check(fixture.Completed==1 && fixture.Counts[1]==1,"timestamped log replay and duplicate rejection");
            Check(fixture.Records[0].Seconds==14,"replay timestamps preserve elapsed");
            var arm5=new MonitorModel("ARM5 full workflow");
            arm5.Process(Arm(1,0,0),T);arm5.Process(Arm(2,0,100),T.AddSeconds(2));
            arm5.Process(Arm(3,2,105),T.AddSeconds(4));arm5.Process(Arm(4,2,105),T.AddSeconds(4));arm5.Process(Arm(0,2,105),T.AddSeconds(15));
            Check(arm5.Completed==1 && arm5.Counts[2]==1,"ARM5 ordered command/motion/done may share last inference frame");
            arm5.Process(Arm(0,2,105),T.AddSeconds(16));Check(arm5.Completed==1,"ARM5 duplicate done never recounted");
            var locate=new MonitorModel("ARM5 locate only");locate.Process(Arm(1,0,0),T);locate.Process(Arm(0,0,0),T.AddSeconds(2));
            Check(locate.Completed==0 && locate.Errors==0,"localization-only is not a sort or recognition failure");
            var noCommand=new MonitorModel("ARM5 incomplete");noCommand.Process(Arm(1,0,0),T);noCommand.Process(Arm(2,0,100),T);
            noCommand.Process(Arm(4,2,105),T);noCommand.Process(Arm(0,2,105),T);
            Check(noCommand.Completed==0,"same-frame support still requires accepted command event");
            var rounds=new MonitorModel("ARM5 repeated zero trigger");
            for(int round=0;round<3;round++) {
                rounds.Process(Arm(1,2,0),T.AddSeconds(round*20));
                rounds.Process(Arm(1,2,0),T.AddSeconds(round*20));
                rounds.Process(Arm(2,2,(uint)(100+round*20)),T);
                rounds.Process(Arm(3,2,(uint)(105+round*20)),T);
                rounds.Process(Arm(4,2,(uint)(105+round*20)),T);
                rounds.Process(Arm(0,2,(uint)(105+round*20)),T);
                rounds.Process(Arm(0,2,(uint)(105+round*20)),T);
                Check(rounds.Counts[2]==round+1,"every independent zero-frame trigger counts once");
            }
            Check(rounds.Records.Count==3,"all three completion records retained");
            rounds.Process(Arm(1,2,0),T);rounds.Process(Arm(2,2,100),T);
            rounds.Process(Arm(3,2,105),T);rounds.Process(Arm(4,2,105),T);rounds.Process(Arm(0,2,105),T);
            Check(rounds.Counts[2]==3,"replayed old full cycle not recounted");
            rounds.Process("YOLO accelerator started",T);Complete(rounds,2,10);
            Check(rounds.Counts[2]==4,"boot resets completion-frame watermark");
            rounds.BreakConnection("test");Complete(rounds,2,1);
            Check(rounds.Counts[2]==5,"new connection permits a new low-frame complete observation");
            Console.WriteLine("PARSER_TESTS_PASS assertions="+assertions);return 0;
        }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
    }
}
