using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace MetalDefectMonitor
{
    public sealed class LineFramer
    {
        readonly StringBuilder pending = new StringBuilder();
        bool dropping;
        public int OversizeLines { get; private set; }
        public List<string> Push(string chunk)
        {
            var lines = new List<string>();
            foreach (char c in chunk)
            {
                if (c == '\n')
                {
                    if (!dropping) { string line = pending.ToString().TrimEnd('\r'); if (line.Length > 0) lines.Add(line); }
                    pending.Length = 0; dropping = false;
                }
                else if (!dropping)
                {
                    if (pending.Length >= 8192) { pending.Length = 0; dropping = true; OversizeLines++; }
                    else if (c != '\0') pending.Append(c);
                }
            }
            return lines;
        }
        public void Reset() { pending.Length = 0; dropping = false; }
    }

    public sealed class CycleRecord
    {
        public string Source, Outcome, Category, Note;
        public uint StartFrame, EndFrame, Samples;
        public ulong Observation;
        public DateTime? At;
        public double? Seconds;
    }

    public sealed class Detection
    {
        public uint Frame, Id;
        public int ClassId, ScorePpm, X1, Y1, X2, Y2;
        public DateTime? At;
    }

    public sealed class MonitorModel
    {
        public static readonly string[] Classes = { "无缺陷", "划痕", "凹陷", "脏污" };
        public static readonly string[] States = { "等待下一轮", "等待画面稳定", "采样判定", "已发出动作指令", "机械臂动作中", "本轮失败 / 待复核" };
        public static readonly string[] Reasons = { "未报告异常", "输入无效", "类别冲突超时", "未形成稳定共识", "动作启动超时", "动作执行超时",
            "保留", "保留", "保留", "保留", "五舵机标定加载失败", "回初始位置失败", "观察位置未记录或标定无效", "返回观察位置失败",
            "内框或圆片定位失败", "圆片坐标超出范围", "移动到圆片上方失败", "机械臂阶段超时", "分类抓取指令失败", "保留", "保留",
            "分类输入无效", "类别冲突超时", "未形成稳定共识", "分类动作启动超时", "分类动作执行超时" };
        static readonly Regex Arm = new Regex(@"^ARM_SORT\s+state=(\d+)\s+result=(\d+)\s+samples=(\d+)\s+frame=(\d+)\s+reason=(\d+)\s+mixed=(\d+)\s*$", RegexOptions.CultureInvariant);
        static readonly Regex Defect = new Regex(@"^DEFECT\s+frame=(\d+)\s+id=(\d+)\s+class=(scratch|dent|stain)\s+score_ppm=(\d+)\s+box=(-?\d+),(-?\d+),(-?\d+),(-?\d+)\s*$", RegexOptions.CultureInvariant);
        static readonly Regex Inference = new Regex(@"^YOLO inference:\s*(\d+)\s+us\s*$", RegexOptions.CultureInvariant);
        public readonly long[] Counts = new long[4];
        public readonly List<CycleRecord> Records = new List<CycleRecord>();
        public readonly List<Detection> Detections = new List<Detection>();
        public readonly Queue<double> InferenceHistory = new Queue<double>();
        public string Source { get; private set; }
        public bool HasArm, HasFrame;
        public int ArmState, Result, Reason;
        public uint Samples, Mixed, Frame;
        public double? InferenceMs;
        public DateTime? LastRecognizedAt;
        public int RecognizedLines, IgnoredLines, RejectedLines, Errors;
        public string Notice = "等待开发板已有串口日志；未收到数据不表示设备正常。";
        public string LastCompletion = "尚未观察到完整分拣流程";
        bool cycleActive;
        int phase, selected = -1;
        uint cycleStart, cycleFrame;
        DateTime? cycleTime;
        readonly HashSet<string> recorded = new HashSet<string>();
        string lastArm = "";
        int epoch;
        ulong cycleOrdinal;
        bool hasCompletedFrame;
        uint lastCompletedFrame;

        public MonitorModel(string source) { Source = source; }
        public static void UnpackRecordedLine(string raw,out string text,out DateTime? at)
        {
            text=raw;at=null;
            int tab=raw.IndexOf('\t');DateTime parsed;
            if(tab>0 && DateTime.TryParse(raw.Substring(0,tab),CultureInfo.InvariantCulture,DateTimeStyles.RoundtripKind,out parsed))
            {at=parsed;text=raw.Substring(tab+1);}
        }
        public long Completed { get { long total = 0; foreach (long n in Counts) total += n; return total; } }
        public bool CycleActive { get { return cycleActive; } }

        public void BreakConnection(string why)
        {
            Abandon(why, null); HasArm = false; HasFrame = false; lastArm = ""; epoch++;
            hasCompletedFrame = false;
            Notice = "连接中断期间的动作不补算；重新连接后需完整观察一轮。";
        }
        void Abandon(string note, DateTime? at)
        {
            if (cycleActive) AddRecord("未完整观察 / 不计数", "未确认", at, cycleFrame, 0, note);
            cycleActive = false; phase = 0; selected = -1;
        }
        static bool Forward(uint a, uint b) { return unchecked((int)(a - b)) > 0; }
        void AddRecord(string outcome, string category, DateTime? at, uint frame, uint samples, string note)
        {
            double? elapsed = null;
            if (at.HasValue && cycleTime.HasValue && at.Value >= cycleTime.Value)
                elapsed = (at.Value - cycleTime.Value).TotalSeconds;
            Records.Insert(0, new CycleRecord { Source = Source, Outcome = outcome, Category = category,
                StartFrame = cycleStart, EndFrame = frame, Samples = samples, At = at,
                Observation = cycleOrdinal,
                Seconds = elapsed, Note = note });
            if (Records.Count > 10000) Records.RemoveAt(Records.Count - 1);
        }
        bool RejectArm(DateTime? at)
        {
            RejectedLines++;
            Abandon("状态日志格式或取值无效，本轮不计数",at);
            HasArm=false;
            Notice="收到无效状态日志；本轮观察已作废，请检查波特率或日志完整性。";
            return false;
        }
        public bool Process(string text, DateTime? at)
        {
            if (text == null || text.Length > 8192) { RejectedLines++; return false; }
            string line = text.Trim();
            if (line == "YOLO accelerator started")
            {
                Abandon("收到程序启动标记，上轮未完成记录不计数", at);
                epoch++; lastArm = ""; HasArm = false; HasFrame = false; Detections.Clear();
                hasCompletedFrame = false;
                Notice = "检测程序已启动。计数仍仅代表本机观察到的动作完成，不是板端总产量。";
                RecognizedLines++; LastRecognizedAt = at; return true;
            }
            Match m = Arm.Match(line);
            if (m.Success)
            {
                uint st, result, samples, frame, reason, mixed;
                if (!UInt32.TryParse(m.Groups[1].Value, out st) || st > 5 ||
                    !UInt32.TryParse(m.Groups[2].Value, out result) || result > 3 ||
                    !UInt32.TryParse(m.Groups[3].Value, out samples) || samples > 1000000 ||
                    !UInt32.TryParse(m.Groups[4].Value, out frame) ||
                    !UInt32.TryParse(m.Groups[5].Value, out reason) || reason > 25 || (reason > 5 && reason < 10) || reason == 19 || reason == 20 ||
                    !UInt32.TryParse(m.Groups[6].Value, out mixed)) return RejectArm(at);
                LastRecognizedAt = at; RecognizedLines++;
                if (line == lastArm) return true;
                lastArm = line;
                // ARM5 resets its classification frame to zero at each trigger.
                // A trigger is a new observation, not a stale inference frame.
                if (st != 1 && cycleActive && frame != cycleFrame && !Forward(frame, cycleFrame))
                    Abandon("帧号回退或过期日志，未计数", at);
                ArmState = (int)st; Result = (int)result; Samples = samples;
                Reason = (int)reason; Mixed = mixed; Frame = frame; HasFrame = true; HasArm = true;
                if (st == 1)
                {
                    if (cycleActive && cycleStart == frame) return true;
                    Abandon("新触发到来，上一轮日志未完整", at);
                    cycleActive = true; cycleStart = cycleFrame = frame; cycleTime = at; phase = 1; selected = -1;
                    cycleOrdinal++;
                    Notice = "已观察到单次触发。等待稳定和采样；以板端状态变化为准。";
                }
                else if (st == 5)
                {
                    if (cycleActive)
                    {
                        string key = epoch + ":" + cycleOrdinal + ":error";
                        if (recorded.Add(key)) { Errors++; AddRecord("失败 / 待复核", "未确认", at, frame, samples, Reasons[reason]); }
                    }
                    cycleActive = false; phase = 0; selected = -1;
                    Notice = Reasons[reason] + "。请检查实物与板端状态；不会自动解除锁定或重试。";
                }
                else if (cycleActive)
                {
                    bool progression = frame != cycleFrame && Forward(frame, cycleFrame);
                    // ARM5 can accept K, start motion and return without a newer
                    // classification frame. Ordered distinct events still matter.
                    bool eventProgression = frame == cycleFrame || progression;
                    if (st == 2 && phase == 1 && progression) phase = 2;
                    else if (st == 3 && phase == 2 && eventProgression && samples >= 5) { phase = 3; selected = (int)result; }
                    else if (st == 4 && phase == 3 && eventProgression && selected == result) phase = 4;
                    else if (st == 0 && phase == 4 && eventProgression && selected == result)
                    {
                        string key = epoch + ":" + cycleOrdinal + ":complete";
                        // A replay of an entire old sequence must not count again.
                        if ((!hasCompletedFrame || Forward(frame, lastCompletedFrame)) && recorded.Add(key))
                        {
                            hasCompletedFrame = true; lastCompletedFrame = frame;
                            Counts[result]++; LastCompletion = Classes[result];
                            AddRecord("动作时序完成", Classes[result], at, frame, samples, "不等于已传感确认抓取或落篮成功");
                        }
                        else AddRecord("过期完成 / 不计数", Classes[result], at, frame, samples, "完成帧未前进，拒绝重复或过期流程");
                        cycleActive = false; phase = 0; selected = -1;
                        Notice = "观察到完整返回流程。可摆下一片并按 PL_KEY1；抓取成功仍需实物确认。";
                    }
                    else if (!(st == phase && progression)) Abandon("缺失、过期或顺序不一致的状态日志，不计数", at);
                    cycleFrame = frame;
                }
                else if (st == 0) Notice = "等待下一轮；无完整流程记录时不补算次数。";
                else Notice = "连接时已处于流程中；本轮日志不完整，不计入完成次数。";
                return true;
            }
            m = Inference.Match(line);
            if (m.Success)
            {
                uint us;
                if (!UInt32.TryParse(m.Groups[1].Value, out us) || us == 0 || us > 60000000) { RejectedLines++; return false; }
                InferenceMs = us / 1000.0; InferenceHistory.Enqueue(InferenceMs.Value);
                while (InferenceHistory.Count > 60) InferenceHistory.Dequeue();
                LastRecognizedAt = at; RecognizedLines++; return true;
            }
            m = Defect.Match(line);
            if (m.Success)
            {
                uint frame, id; int score, x1, y1, x2, y2;
                if (!UInt32.TryParse(m.Groups[1].Value, out frame) || !UInt32.TryParse(m.Groups[2].Value, out id) ||
                    !Int32.TryParse(m.Groups[4].Value, out score) || score < 0 || score > 1000000 ||
                    !Int32.TryParse(m.Groups[5].Value, out x1) || !Int32.TryParse(m.Groups[6].Value, out y1) ||
                    !Int32.TryParse(m.Groups[7].Value, out x2) || !Int32.TryParse(m.Groups[8].Value, out y2) ||
                    x1 < 0 || y1 < 0 || x2 < x1 || y2 < y1 || x2 > 10000 || y2 > 10000) { RejectedLines++; return false; }
                int cls = m.Groups[3].Value == "scratch" ? 1 : m.Groups[3].Value == "dent" ? 2 : 3;
                if (Detections.Count > 0 && Detections[0].Frame != frame) Detections.Clear();
                Detections.RemoveAll(delegate(Detection d) { return d.Frame == frame && d.Id == id; });
                Detections.Add(new Detection { Frame = frame, Id = id, ClassId = cls, ScorePpm = score,
                    X1 = x1, Y1 = y1, X2 = x2, Y2 = y2, At = at });
                if (Detections.Count > 20) Detections.RemoveAt(0);
                if (!HasFrame || Forward(frame, Frame)) { Frame = frame; HasFrame = true; }
                LastRecognizedAt = at; RecognizedLines++; return true;
            }
            if (line.StartsWith("ERROR:", StringComparison.Ordinal) || line.IndexOf("FAILED", StringComparison.Ordinal) >= 0)
                Notice = "串口出现错误提示，请查看原始日志；本程序不自动恢复开发板。";
            if (line.StartsWith("ARM_SORT ", StringComparison.Ordinal)) return RejectArm(at);
            if (line.StartsWith("DEFECT ", StringComparison.Ordinal)) RejectedLines++;
            else IgnoredLines++;
            return false;
        }

        static string Quote(string value)
        {
            value = value ?? "";
            if (value.Length > 0 && "=+-@".IndexOf(value[0]) >= 0) value = "'" + value;
            return "\"" + value.Replace("\"", "\"\"") + "\"";
        }
        public string ExportCsv()
        {
            var s = new StringBuilder("数据来源,电脑接收时间,记录类型,分类,触发帧,结束帧,采样帧数,观察耗时秒,说明,本机轮次\r\n");
            foreach (CycleRecord row in Records)
            {
                string[] cells = { row.Source, row.At.HasValue ? row.At.Value.ToString("yyyy-MM-dd HH:mm:ss.fff", CultureInfo.InvariantCulture) : "无时间戳",
                    row.Outcome, row.Category, row.StartFrame.ToString(CultureInfo.InvariantCulture), row.EndFrame.ToString(CultureInfo.InvariantCulture),
                    row.Samples.ToString(CultureInfo.InvariantCulture), row.Seconds.HasValue ? row.Seconds.Value.ToString("F3", CultureInfo.InvariantCulture) : "不可用", row.Note,
                    row.Observation.ToString(CultureInfo.InvariantCulture) };
                for (int i = 0; i < cells.Length; i++) { if (i > 0) s.Append(','); s.Append(Quote(cells[i])); }
                s.Append("\r\n");
            }
            return s.ToString();
        }
    }
}
