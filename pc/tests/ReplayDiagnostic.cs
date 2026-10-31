using System;
using System.IO;
using MetalDefectMonitor;
class ReplayDiagnostic
{
    static int Main(string[] args)
    {
        if(args.Length!=1)return 2;
        var model=new MonitorModel("真实串口离线回放 / 不操作开发板");
        using(var stream=new FileStream(args[0],FileMode.Open,FileAccess.Read,FileShare.ReadWrite))
        using(var reader=new StreamReader(stream)) {
        string raw;
        while((raw=reader.ReadLine())!=null) {
            string text;DateTime? at;MonitorModel.UnpackRecordedLine(raw,out text,out at);
            model.Process(text,at);
        }
        }
        Console.WriteLine("REPLAY_ONLY normal={0} scratch={1} dent={2} stain={3} completed={4} errors={5}",
            model.Counts[0],model.Counts[1],model.Counts[2],model.Counts[3],model.Completed,model.Errors);
        foreach(CycleRecord r in model.Records)
            if(r.Outcome=="动作时序完成")Console.WriteLine("round={0} class={1} end_frame={2} at={3:O}",r.Observation,r.Category,r.EndFrame,r.At);
        return 0;
    }
}
