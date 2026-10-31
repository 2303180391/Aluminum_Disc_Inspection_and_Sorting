using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace MetalDefectMonitor
{
    sealed class ReceivedLine
    {
        public string Text;
        public DateTime At;
        public ReceivedLine(string text, DateTime at) { Text=text; At=at; }
    }

    // No free-text transmitter: only the bounded CRC/session protocol is sent.
    sealed class ArmSerialClient : IDisposable
    {
        readonly SerialPort port;
        readonly Thread thread;
        volatile bool closing;
        public volatile string Error;
        public int Lost;
        public readonly ConcurrentQueue<ReceivedLine> Lines = new ConcurrentQueue<ReceivedLine>();
        public ArmSerialClient(string name)
        {
            port = new SerialPort(name,115200,Parity.None,8,StopBits.One);
            port.Handshake=Handshake.None; port.DtrEnable=false; port.RtsEnable=false;
            port.ReadTimeout=250; port.WriteTimeout=400; port.Encoding=Encoding.ASCII; port.ReadBufferSize=65536;
            try { port.Open();port.DiscardInBuffer();port.DiscardOutBuffer(); } catch { port.Dispose(); throw; }
            thread=new Thread(ReadLoop); thread.IsBackground=true; thread.Name="Arm UART RX"; thread.Start();
        }
        public void Send(string wire)
        {
            if(closing || wire==null || wire.Length>64 || !wire.StartsWith("@ARM1,",StringComparison.Ordinal) || !wire.EndsWith("\n",StringComparison.Ordinal))throw new InvalidOperationException("非法控制帧。");
            port.Write(wire);
        }
        void ReadLoop()
        {
            var framer=new LineFramer(); var buffer=new char[2048];
            while(!closing)
            {
                try {
                    int count=port.Read(buffer,0,buffer.Length);
                    int oversized=framer.OversizeLines;
                    foreach(string line in framer.Push(new string(buffer,0,count)))
                    {
                        if(Lines.Count>=3000) Interlocked.Exchange(ref Lost,1);
                        else Lines.Enqueue(new ReceivedLine(line,DateTime.Now));
                    }
                    if(framer.OversizeLines!=oversized) Interlocked.Exchange(ref Lost,1);
                } catch(TimeoutException) { }
                  catch(Exception ex) { if(!closing) Error=ex.Message; break; }
            }
        }
        public void Dispose()
        {
            closing=true;
            try { port.Close(); } catch { }
            if(thread!=Thread.CurrentThread) thread.Join(600);
            port.Dispose();
        }
    }

    sealed class Surface : Panel
    {
        public Surface() { DoubleBuffered=true; BackColor=Theme.Panel; }
    }

    sealed class ControlButton : Button
    {
        protected override void OnPaint(PaintEventArgs e)
        {
            if(Enabled){base.OnPaint(e);return;}
            using(var brush=new SolidBrush(Theme.PanelLight))e.Graphics.FillRectangle(brush,ClientRectangle);
            TextRenderer.DrawText(e.Graphics,Text,Font,ClientRectangle,Theme.Muted,
                TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis);
        }
    }

    static class Theme
    {
        public static readonly Color Background=Color.FromArgb(13,21,34), Panel=Color.FromArgb(23,35,52),
            PanelLight=Color.FromArgb(31,46,65), Text=Color.FromArgb(232,240,250), Muted=Color.FromArgb(147,167,190),
            Accent=Color.FromArgb(46,211,196), Amber=Color.FromArgb(255,191,83), Red=Color.FromArgb(255,121,132);
        public static readonly Color[] Category={Color.FromArgb(77,217,159),Color.FromArgb(255,193,92),Color.FromArgb(106,176,255),Color.FromArgb(190,153,255)};
        public static Font Font(float size,FontStyle style) { return new Font("Microsoft YaHei UI",size,style,GraphicsUnit.Point); }
        public static Label Label(string text,float size,Color color,FontStyle style)
        { return new Label {Text=text,Font=Font(size,style),ForeColor=color,AutoSize=false,TextAlign=ContentAlignment.MiddleLeft,AutoEllipsis=true}; }
        public static Button Button(string text,bool primary)
        {
            var b=new ControlButton {Text=text,Height=34,Width=110,FlatStyle=FlatStyle.Flat,Cursor=Cursors.Hand,
                BackColor=primary?Accent:PanelLight,ForeColor=primary?Background:Text,Font=Font(9,FontStyle.Bold),Margin=new Padding(5,3,5,3)};
            b.FlatAppearance.BorderSize=0;return b;
        }
    }

    sealed class MainForm : Form
    {
        readonly string root=AppDomain.CurrentDomain.BaseDirectory;
        readonly Label[] numbers=new Label[4];
        readonly Label[] metricValues=new Label[6];
        Label modeLabel,connectionLabel,stateLabel,stateDetail,noticeLabel,footer,summary;
        ComboBox ports;
        Button connect,demoButton,resetButton,replayButton,enableArm,startArm,disableArm,retryArm,handshakeArm,teachArm,locateArm;
        Label controlHint,controlWarning;
        readonly ArmControl control=new ArmControl();
        DataGridView history,targets;
        TextBox log;
        readonly System.Windows.Forms.Timer timer=new System.Windows.Forms.Timer();
        readonly System.Windows.Forms.Timer demoTimer=new System.Windows.Forms.Timer();
        MonitorModel live=new MonitorModel("实时串口 / 本机观察"), active;
        ArmSerialClient client;
        StreamWriter rawWriter;
        string csvPath;
        DateTime? lastRaw;
        CycleRecord renderedFirst;
        MonitorModel renderedModel;
        bool isDemo,isReplay,renderDirty=true,renderedFresh;
        int linesReceived,demoStep;

        public MainForm()
        {
            Text="五舵机定位识别分拣控制台 | ARM5 / 轮次修复版";
            ClientSize=new Size(1280,900);MinimumSize=new Size(1130,820);
            StartPosition=FormStartPosition.CenterScreen;BackColor=Theme.Background;
            ForeColor=Theme.Text;Font=Theme.Font(9,FontStyle.Regular);AutoScaleMode=AutoScaleMode.Dpi;
            active=live;BuildUi();RefreshPorts();RefreshView();
            timer.Interval=150;timer.Tick+=Tick;timer.Start();
            demoTimer.Interval=950;demoTimer.Tick+=delegate {DemoNext();};
            FormClosing+=delegate { timer.Stop();demoTimer.Stop();CloseSerial(true); };
        }

        void BuildUi()
        {
            var layout=new TableLayoutPanel {Dock=DockStyle.Fill,Padding=new Padding(20,16,20,12),ColumnCount=1,RowCount=8,BackColor=Theme.Background};
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute,82));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute,63));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute,112));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute,116));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute,150));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute,47));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent,100));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute,47));Controls.Add(layout);

            var header=new Panel {Dock=DockStyle.Fill,Margin=new Padding(0)};
            var eyebrow=Theme.Label("FPGA VISION-GUIDED SORTING   /   ARM5",9,Theme.Accent,FontStyle.Bold);
            eyebrow.SetBounds(0,0,650,20);header.Controls.Add(eyebrow);
            var title=Theme.Label("定位 · 识别 · 抓取 · 分拣",23,Theme.Text,FontStyle.Bold);
            title.SetBounds(0,23,670,44);header.Controls.Add(title);
            modeLabel=Theme.Label("五舵机控制 · 需加载配套 ARM5 ELF",10,Theme.Accent,FontStyle.Bold);
            modeLabel.TextAlign=ContentAlignment.MiddleRight;modeLabel.Dock=DockStyle.Right;modeLabel.Width=360;header.Controls.Add(modeLabel);
            layout.Controls.Add(header,0,0);

            var connection=new Surface {Dock=DockStyle.Fill,Margin=new Padding(0,0,0,12),Padding=new Padding(12,7,12,7)};
            var row=new FlowLayoutPanel {Dock=DockStyle.Fill,WrapContents=false,FlowDirection=FlowDirection.LeftToRight};
            var portLabel=Theme.Label("PS_UART",10,Theme.Text,FontStyle.Bold);portLabel.Width=110;portLabel.Height=34;row.Controls.Add(portLabel);
            ports=new ComboBox {DropDownStyle=ComboBoxStyle.DropDownList,Width=115,Height=32,Font=Theme.Font(11,FontStyle.Regular),Margin=new Padding(4,3,8,0)};row.Controls.Add(ports);
            var refresh=Theme.Button("刷新串口",false);refresh.Width=92;refresh.Click+=delegate {RefreshPorts();};row.Controls.Add(refresh);
            var settings=Theme.Label("115200 · 8N1",9,Theme.Muted,FontStyle.Regular);settings.Width=127;settings.Height=34;settings.Margin=new Padding(12,0,4,0);row.Controls.Add(settings);
            connect=Theme.Button("连接串口",true);connect.Click+=delegate {ToggleConnection();};row.Controls.Add(connect);
            connectionLabel=Theme.Label("未连接 · 请接 PS_UART，不是 PL_UART / USB_SLAVE",9,Theme.Muted,FontStyle.Regular);
            connectionLabel.Width=450;connectionLabel.Height=34;connectionLabel.Margin=new Padding(18,0,0,0);row.Controls.Add(connectionLabel);
            row.SizeChanged+=delegate {connectionLabel.Width=Math.Max(180,row.ClientSize.Width-640);};
            connection.Controls.Add(row);layout.Controls.Add(connection,0,1);

            var controller=new Surface {Dock=DockStyle.Fill,Margin=new Padding(0,0,0,12),Padding=new Padding(8,4,8,3)};
            var controls=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=1,RowCount=3};
            controls.RowStyles.Add(new RowStyle(SizeType.Absolute,40));controls.RowStyles.Add(new RowStyle(SizeType.Absolute,24));controls.RowStyles.Add(new RowStyle(SizeType.Percent,100));
            var buttons=new FlowLayoutPanel {Dock=DockStyle.Fill,WrapContents=false};
            enableArm=Theme.Button("使能 / 初始姿态",false);enableArm.Width=133;enableArm.Click+=delegate {RequestArm("ENABLE");};buttons.Controls.Add(enableArm);
            teachArm=Theme.Button("记录当前观察位",false);teachArm.Width=143;teachArm.Click+=delegate {RequestArm("TEACH");};buttons.Controls.Add(teachArm);
            locateArm=Theme.Button("仅定位验证",false);locateArm.Width=108;locateArm.Click+=delegate {RequestArm("LOCATE");};buttons.Controls.Add(locateArm);
            startArm=Theme.Button("定位→识别→分拣",true);startArm.Width=153;startArm.Click+=delegate {RequestArm("START");};buttons.Controls.Add(startArm);
            disableArm=Theme.Button("机械臂失能",false);disableArm.Width=113;disableArm.Click+=delegate {RequestArm("DISABLE");};buttons.Controls.Add(disableArm);
            retryArm=Theme.Button("解除识别失败",false);retryArm.Width=121;retryArm.Click+=delegate {RequestArm("RETRY");};buttons.Controls.Add(retryArm);
            handshakeArm=Theme.Button("重新握手",false);handshakeArm.Width=96;handshakeArm.Click+=delegate {BeginHandshake();};buttons.Controls.Add(handshakeArm);
            controlHint=Theme.Label("控制未握手 · 按钮锁定",9,Theme.Muted,FontStyle.Bold);controlHint.Dock=DockStyle.Fill;
            controls.Controls.Add(buttons,0,0);
            controls.Controls.Add(controlHint,0,1);
            controlWarning=Theme.Label("失能不是急停：PWM关闭后可能松爪或下垂。动作期间不允许失能；断开串口不会停止已开始的动作。",8,Theme.Amber,FontStyle.Regular);
            controlWarning.Dock=DockStyle.Fill;controls.Controls.Add(controlWarning,0,2);controller.Controls.Add(controls);layout.Controls.Add(controller,0,2);

            var cards=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=4,RowCount=1,Margin=new Padding(0,0,0,12)};
            for(int i=0;i<4;i++)
            {
                cards.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,25));
                var card=new Surface {Dock=DockStyle.Fill,Margin=new Padding(i==0?0:6,0,i==3?0:6,0),Padding=new Padding(17,8,12,8)};
                var content=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=2,RowCount=2,Margin=new Padding(0)};
                content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,62));content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,38));
                content.RowStyles.Add(new RowStyle(SizeType.Percent,55));content.RowStyles.Add(new RowStyle(SizeType.Percent,45));
                var label=Theme.Label(MonitorModel.Classes[i],14,Theme.Category[i],FontStyle.Bold);label.Dock=DockStyle.Fill;content.Controls.Add(label,0,0);
                numbers[i]=Theme.Label("0",30,Theme.Text,FontStyle.Bold);numbers[i].Dock=DockStyle.Fill;numbers[i].TextAlign=ContentAlignment.MiddleRight;content.Controls.Add(numbers[i],1,0);content.SetRowSpan(numbers[i],2);
                var sub=Theme.Label("本机观察完成次数",8,Theme.Muted,FontStyle.Regular);sub.Dock=DockStyle.Fill;content.Controls.Add(sub,0,1);
                card.Controls.Add(content);cards.Controls.Add(card,i,0);
            }
            layout.Controls.Add(cards,0,3);

            var status=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=2,RowCount=1,Margin=new Padding(0,0,0,12)};
            status.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,57));status.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,43));
            var current=new Surface {Dock=DockStyle.Fill,Margin=new Padding(0,0,6,0),Padding=new Padding(16,10,16,8)};
            var currentGrid=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=1,RowCount=4};
            currentGrid.RowStyles.Add(new RowStyle(SizeType.Absolute,23));currentGrid.RowStyles.Add(new RowStyle(SizeType.Absolute,44));currentGrid.RowStyles.Add(new RowStyle(SizeType.Absolute,24));currentGrid.RowStyles.Add(new RowStyle(SizeType.Percent,100));
            var stateTitle=Theme.Label("当前分拣流程 / 来自状态日志",9,Theme.Muted,FontStyle.Regular);stateTitle.Dock=DockStyle.Fill;currentGrid.Controls.Add(stateTitle,0,0);
            stateLabel=Theme.Label("等待连接",22,Theme.Text,FontStyle.Bold);stateLabel.Dock=DockStyle.Fill;currentGrid.Controls.Add(stateLabel,0,1);
            stateDetail=Theme.Label("等待 → 稳定 → 采样 → 指令 → 动作 → 返回",9,Theme.Muted,FontStyle.Regular);stateDetail.Dock=DockStyle.Fill;currentGrid.Controls.Add(stateDetail,0,2);
            noticeLabel=Theme.Label("",8,Theme.Muted,FontStyle.Regular);noticeLabel.Dock=DockStyle.Fill;currentGrid.Controls.Add(noticeLabel,0,3);current.Controls.Add(currentGrid);status.Controls.Add(current,0,0);
            var metrics=new Surface {Dock=DockStyle.Fill,Margin=new Padding(6,0,0,0),Padding=new Padding(14,9,10,9)};
            var grid=new TableLayoutPanel {Dock=DockStyle.Fill,RowCount=6,ColumnCount=2};grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,43));grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,57));
            string[] metricNames={"板端帧号","本轮采样帧数","最近推理耗时","票数 正常/划/凹/污","圆心坐标（毫米）","观察位 / 标定"};
            for(int i=0;i<6;i++) {grid.RowStyles.Add(new RowStyle(SizeType.Percent,100f/6));var key=Theme.Label(metricNames[i],9,Theme.Muted,FontStyle.Regular);key.Dock=DockStyle.Fill;grid.Controls.Add(key,0,i);metricValues[i]=Theme.Label("--",10,Theme.Text,FontStyle.Bold);metricValues[i].Dock=DockStyle.Fill;grid.Controls.Add(metricValues[i],1,i);}
            metrics.Controls.Add(grid);status.Controls.Add(metrics,1,0);layout.Controls.Add(status,0,4);

            var actions=new FlowLayoutPanel {Dock=DockStyle.Fill,WrapContents=false,Margin=new Padding(0),Padding=new Padding(0,0,0,4)};
            var export=Theme.Button("导出记录 CSV",false);export.Width=126;export.Click+=delegate {Export();};actions.Controls.Add(export);
            replayButton=Theme.Button("回放日志",false);replayButton.Click+=delegate {Replay();};actions.Controls.Add(replayButton);
            demoButton=Theme.Button("演示预览",false);demoButton.Click+=delegate {if(isDemo)LeaveOffline();else EnterDemo();};actions.Controls.Add(demoButton);
            resetButton=Theme.Button("清空电脑统计",false);resetButton.Width=128;resetButton.Click+=delegate {ResetStatistics();};actions.Controls.Add(resetButton);
            summary=Theme.Label("已观察完成 0 次 | 异常轮次 0",9,Theme.Muted,FontStyle.Regular);summary.Width=410;summary.Height=34;summary.Margin=new Padding(16,0,0,0);actions.Controls.Add(summary);layout.Controls.Add(actions,0,5);

            var body=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=2,RowCount=1,Margin=new Padding(0)};
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,62));body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,38));
            var tabs=new TabControl {Dock=DockStyle.Fill,Margin=new Padding(0,0,8,0),Font=Theme.Font(9,FontStyle.Regular)};
            var recordsPage=new TabPage("动作记录（非抓取成功证明）") {BackColor=Theme.Panel,Padding=new Padding(5)};
            history=MakeGrid(new[]{"电脑时间","结果类别","记录类型","轮次 / 结束帧","采样","观察秒"},new[]{115,75,150,115,48,65});recordsPage.Controls.Add(history);tabs.TabPages.Add(recordsPage);
            var targetsPage=new TabPage("最近目标（每30帧的周期日志）") {BackColor=Theme.Panel,Padding=new Padding(5)};
            targets=MakeGrid(new[]{"日志帧","目标ID","类别","置信度","框坐标"},new[]{75,65,85,85,260});targetsPage.Controls.Add(targets);tabs.TabPages.Add(targetsPage);body.Controls.Add(tabs,0,0);
            var rawPanel=new Surface {Dock=DockStyle.Fill,Margin=new Padding(4,0,0,0),Padding=new Padding(12,8,12,10)};
            var rawLayout=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=1,RowCount=2};rawLayout.RowStyles.Add(new RowStyle(SizeType.Absolute,27));rawLayout.RowStyles.Add(new RowStyle(SizeType.Percent,100));
            var rawTitle=Theme.Label("串口日志 · TX指令 / 板端ACK与状态",9,Theme.Accent,FontStyle.Bold);rawTitle.Dock=DockStyle.Fill;rawLayout.Controls.Add(rawTitle,0,0);
            log=new TextBox {Dock=DockStyle.Fill,Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Both,WordWrap=false,
                BackColor=Theme.Background,ForeColor=Color.FromArgb(164,198,218),BorderStyle=BorderStyle.None,Font=new Font("Consolas",9),MaxLength=120000};
            rawLayout.Controls.Add(log,0,1);rawPanel.Controls.Add(rawLayout);body.Controls.Add(rawPanel,1,0);layout.Controls.Add(body,0,6);
            footer=Theme.Label("HDMI显示视频 | 坐标基于100×100毫米内框 | 先记录观察位，再定位验证、完整分拣",8,Theme.Muted,FontStyle.Regular);
            footer.Dock=DockStyle.Fill;layout.Controls.Add(footer,0,7);
        }

        DataGridView MakeGrid(string[] titles,int[] widths)
        {
            var g=new DataGridView {Dock=DockStyle.Fill,BackgroundColor=Theme.Panel,BorderStyle=BorderStyle.None,
                ReadOnly=true,AllowUserToAddRows=false,AllowUserToDeleteRows=false,AllowUserToResizeRows=false,
                RowHeadersVisible=false,SelectionMode=DataGridViewSelectionMode.FullRowSelect,MultiSelect=false,
                EnableHeadersVisualStyles=false,AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill,GridColor=Theme.PanelLight};
            g.ColumnHeadersHeight=36;g.ColumnHeadersDefaultCellStyle=new DataGridViewCellStyle {BackColor=Theme.PanelLight,ForeColor=Theme.Text,Font=Theme.Font(9,FontStyle.Bold)};
            g.DefaultCellStyle=new DataGridViewCellStyle {BackColor=Theme.Panel,ForeColor=Theme.Text,SelectionBackColor=Color.FromArgb(42,75,90),SelectionForeColor=Theme.Text,Font=Theme.Font(9,FontStyle.Regular),Padding=new Padding(4,2,4,2)};
            g.RowTemplate.Height=31;g.AlternatingRowsDefaultCellStyle.BackColor=Color.FromArgb(26,39,57);
            for(int i=0;i<titles.Length;i++)g.Columns.Add(new DataGridViewTextBoxColumn {HeaderText=titles[i],Name="col"+i,FillWeight=widths[i],MinimumWidth=42,SortMode=DataGridViewColumnSortMode.NotSortable});
            return g;
        }

        void RefreshPorts()
        {
            string selected=ports.SelectedItem as string;
            try {string[] names=SerialPort.GetPortNames();Array.Sort(names,StringComparer.OrdinalIgnoreCase);ports.Items.Clear();foreach(string name in names)ports.Items.Add(name);
                if(selected!=null && ports.Items.Contains(selected))ports.SelectedItem=selected;else if(ports.Items.Count>0)ports.SelectedIndex=0;}
            catch(Exception ex){AppendLog("[电脑] 无法枚举串口："+ex.Message);}
        }

        void ToggleConnection()
        {
            if(client!=null){CloseSerial(true);RefreshView();return;}
            if(ports.SelectedItem==null){MessageBox.Show(this,"请用支持数据传输的USB线连接开发板 PS_UART，再点刷新串口。\n设备管理器应出现CH340对应的COM口。","未选择串口",MessageBoxButtons.OK,MessageBoxIcon.Information);return;}
            LeaveOffline();
            try {
                var opened=new ArmSerialClient((string)ports.SelectedItem);
                client=opened;active=live;lastRaw=null;linesReceived=0;
                string folder=Path.Combine(root,"logs");Directory.CreateDirectory(folder);
                string stem="serial_"+DateTime.Now.ToString("yyyyMMdd_HHmmss_fff",CultureInfo.InvariantCulture);
                try { rawWriter=new StreamWriter(Path.Combine(folder,stem+".log"),false,new UTF8Encoding(true));rawWriter.AutoFlush=true;csvPath=Path.Combine(folder,stem+"_events.csv"); }
                catch(Exception ex){AppendLog("[电脑] 日志文件不可写，监控仍继续："+ex.Message);}
                AppendLog("[电脑] 已打开 "+ports.SelectedItem+"，115200/8N1，DTR/RTS关闭。仅自动发送无动作握手；不会自动使能或分拣。");
                live.Notice="旧ELF可接收日志，但控制按钮只在配套UART程序握手成功且状态新鲜时开放。";
                BeginHandshake();
                renderDirty=true;RefreshView();
            } catch(Exception ex) {MessageBox.Show(this,"无法打开串口："+ex.Message+"\n\n请关闭占用同一COM口的串口助手/Vitis串口终端。XSCT的JTAG连接可以保留。","连接失败",MessageBoxButtons.OK,MessageBoxIcon.Warning);}
        }

        void CloseSerial(bool recordGap)
        {
            ArmSerialClient old=client;client=null;control.Disconnect();
            if(old!=null){old.Dispose();if(recordGap)live.BreakConnection("串口连接断开，未完整观察");}
            if(rawWriter!=null){try{rawWriter.Dispose();}catch{}rawWriter=null;}
            if(recordGap)SaveEvents();renderDirty=true;
        }

        void AppendLog(string text)
        {
            if(log.TextLength>80000)log.Text=log.Text.Substring(log.TextLength-45000);
            log.AppendText(text+Environment.NewLine);log.SelectionStart=log.TextLength;log.ScrollToCaret();
        }
        void SendWire(string wire)
        {
            try {
                if(client==null || isDemo || isReplay)throw new InvalidOperationException("没有实时串口连接。");
                client.Send(wire);string sent="[电脑TX] "+wire.TrimEnd('\r','\n');AppendLog(sent);
                if(rawWriter!=null)rawWriter.WriteLine(DateTime.Now.ToString("o",CultureInfo.InvariantCulture)+"\t"+sent);
            } catch(Exception ex){CloseSerial(true);AppendLog("[电脑] 指令发送/记录失败，执行结果未知；没有重发："+ex.Message);}
            renderDirty=true;RefreshView();
        }
        void BeginHandshake()
        {
            if(client==null || isDemo || isReplay || control.Pending!=null)return;
            try {SendWire(control.Hello(DateTime.UtcNow));}catch(Exception ex){AppendLog("[电脑] "+ex.Message);}
        }
        void RequestArm(string verb)
        {
            if(client==null || isDemo || isReplay || !control.Allowed(verb,DateTime.UtcNow))return;
            string prompt=verb=="START"?"确认圆片位于100×100毫米内框内，机械臂活动范围无人、无障碍？\n\n将返回观察位、定位圆心、移动到圆片上方、五帧分类、下降抓取、投放并回 HOME。\n首次请先验证定位坐标及机械臂标定位置。":
                verb=="TEACH"?"将当前指令姿态记录为观察位，不会移动机械臂。\n请确认机械臂实际已稳定，夹爪打开，相机完整看到100×100毫米黑色内框。\n固定原点为参考图左下内角，X向右、Y向上；相机方向须保持一致。\n遮挡版每次重新记录观察位后会清除旧定位标定：请先把圆片放在中间、不遮边，执行一次仅定位，再测试局部遮挡。\n记录的是指令角度，不是舵机位置反馈。":
                verb=="LOCATE"?"将返回记录的观察位（可能运动），稳定后计算圆心毫米坐标。\n本次不会前往圆片上方，也不会抓取或投放。\n确认工作区域安全、圆片在内框内？":
                verb=="ENABLE"?"即将开启舵机PWM并回到初始姿态，可能立即运动。\n确认外部供电、共地、接线正常，机械臂活动范围安全？":
                verb=="DISABLE"?"关闭PWM后舵机可能失去保持力，机械臂可能下垂或松爪。\n请先放下工件并支撑机械臂。此按钮不是急停，也不切断外部电源。\n\n确认空闲失能？":
                "仅解除识别失败锁定，不会自动重新抓取。\n请确认实物和工作区域正常，解除后需再点开始一次分拣。";
            if(MessageBox.Show(this,prompt,"机械臂操作确认",MessageBoxButtons.YesNo,MessageBoxIcon.Warning,MessageBoxDefaultButton.Button2)!=DialogResult.Yes)return;
            try {SendWire(control.Request(verb,DateTime.UtcNow));}
            catch(Exception ex){AppendLog("[电脑] 未发送："+ex.Message);renderDirty=true;RefreshView();}
        }
        void SaveEvents()
        {
            if(csvPath==null)return;
            try {File.WriteAllText(csvPath,live.ExportCsv(),new UTF8Encoding(true));}
            catch(Exception ex){AppendLog("[电脑] 记录保存失败："+ex.Message);csvPath=null;}
        }

        void Tick(object sender,EventArgs args)
        {
            if(client!=null)
            {
                if(Interlocked.Exchange(ref client.Lost,0)!=0) {live.BreakConnection("日志过长或接收队列溢出，当前流程不计数");control.Disconnect();AppendLog("[电脑] 日志丢失，控制锁定；请检查机械臂后重新握手。");}
                ReceivedLine item;int drained=0;bool changed=false;
                while(drained++<100 && client!=null && client.Lines.TryDequeue(out item))
                {
                    lastRaw=item.At;linesReceived++;CycleRecord before=live.Records.Count>0?live.Records[0]:null;
                    if(item.Text.StartsWith("ARMCTL ",StringComparison.Ordinal))control.Process(item.Text,item.At.ToUniversalTime());
                    else {live.Process(item.Text,item.At);if(item.Text=="YOLO accelerator started")control.Disconnect();}
                    AppendLog(item.Text);
                    if(rawWriter!=null)try{rawWriter.WriteLine(item.At.ToString("o",CultureInfo.InvariantCulture)+"\t"+item.Text);}catch(Exception ex){AppendLog("[电脑] 原始日志写入失败："+ex.Message);try{rawWriter.Dispose();}catch{}rawWriter=null;}
                    if(live.Records.Count>0 && before!=live.Records[0])changed=true;
                    renderDirty=true;
                }
                if(changed)SaveEvents();
                if(client!=null && client.Error!=null){string error=client.Error;CloseSerial(true);AppendLog("[电脑] 串口已中断："+error);}
            }
            if(control.Pending!=null){string before=control.Pending;control.Expire(DateTime.UtcNow);if(before!=null && control.Pending==null)AppendLog("[电脑] "+control.Message);}
            else control.Expire(DateTime.UtcNow);
            RefreshView();
        }

        void RefreshView()
        {
            bool connected=client!=null;
            modeLabel.Text=isDemo?"演示数据 · 机械臂控制锁定":isReplay?"日志回放 · 机械臂控制锁定":"ARM5 定位控制 · XSCT 加载配套 ELF";
            modeLabel.ForeColor=isDemo||isReplay?Theme.Amber:Theme.Accent;
            connect.Text=connected?"断开串口":"连接串口";ports.Enabled=!connected;demoButton.Text=isDemo?"退出演示":"演示预览";
            if(isDemo||isReplay) {connectionLabel.Text=isDemo?"离线演示 · 未连接任何设备":"离线回放 · 不与开发板通信";connectionLabel.ForeColor=Theme.Amber;}
            else if(!connected){connectionLabel.Text="未连接 · 请接 PS_UART / J12 Type-C";connectionLabel.ForeColor=Theme.Muted;}
            else if(!lastRaw.HasValue){connectionLabel.Text="串口已打开 · 等待握手 / 板端日志";connectionLabel.ForeColor=Theme.Amber;}
            else {
                double age=(DateTime.Now-lastRaw.Value).TotalSeconds;
                connectionLabel.Text=age>45?"长时间无日志 · 请检查CPU暂停/程序/连接":"收到日志 · 最近 "+age.ToString("F0",CultureInfo.InvariantCulture)+" 秒前 | "+linesReceived+" 行";
                connectionLabel.ForeColor=age>45?Theme.Amber:Theme.Accent;
            }
            bool fresh=connected && !isDemo && !isReplay && control.Fresh(DateTime.UtcNow);
            if(fresh!=renderedFresh){renderDirty=true;renderedFresh=fresh;}
            enableArm.Enabled=fresh && control.Allowed("ENABLE",DateTime.UtcNow);startArm.Enabled=fresh && control.Allowed("START",DateTime.UtcNow);
            teachArm.Enabled=fresh && control.Allowed("TEACH",DateTime.UtcNow);locateArm.Enabled=fresh && control.Allowed("LOCATE",DateTime.UtcNow);
            disableArm.Enabled=fresh && control.Allowed("DISABLE",DateTime.UtcNow);retryArm.Enabled=fresh && control.Allowed("RETRY",DateTime.UtcNow);
            handshakeArm.Enabled=connected && !isDemo && !isReplay && control.Pending==null;
            controlHint.Text=isDemo||isReplay?"离线模式：不能发送任何机械臂指令":!connected?"未连接：控制按钮锁定":!fresh?control.Message+(control.Negotiated?" / 状态过期，控制锁定":""):
                control.Pending!=null?"等待确认："+control.Pending:!control.Enabled?"板端确认：PWM失能":control.Busy?"板端确认：回初始 / 动作中":control.State==5?"识别/动作失败：请复核":control.Locked||control.State!=0?"板端确认：识别判定中":"板端确认：已使能，等待下一轮";
            controlHint.ForeColor=fresh?Theme.Accent:Theme.Amber;
            if(fresh && control.Pending==null) {
                if(!control.Arm5)controlHint.Text="已连接旧固件：定位控制锁定，请加载配套 ARM5 新 ELF";
                else if(!control.WorkflowFresh(DateTime.UtcNow))controlHint.Text="等待同一帧状态对应的定位流程数据…";
                else if(control.Phase!=0)controlHint.Text="当前阶段："+control.PhaseName;
                else if(!control.Enabled)controlHint.Text="PWM关闭：先使能并回 HOME";
                else if(!control.ObservationTaught)controlHint.Text="已使能：请确认观察画面，再记录当前观察位";
                else controlHint.Text="观察位已记录：建议先仅定位验证，再启动完整分拣";
            }
            controlHint.AccessibleDescription=control.Message;
            if(fresh && control.Message!=null)noticeLabel.Text=control.Message;
            if(!renderDirty)return;renderDirty=false;
            for(int i=0;i<4;i++)numbers[i].Text=active.Counts[i].ToString(CultureInfo.InvariantCulture);
            stateLabel.Text=active.HasArm?MonitorModel.States[active.ArmState]:(connected?"等待状态日志":isReplay?"回放中尚无状态":isDemo?"演示等待中":"未连接设备");
            stateLabel.ForeColor=active.HasArm&&active.ArmState==5?Theme.Red:Theme.Text;
            stateDetail.Text=active.HasArm&&(active.ArmState==3||active.ArmState==4)?"本轮分类："+MonitorModel.Classes[active.Result]:"最近完整动作："+active.LastCompletion;
            noticeLabel.Text=fresh?control.Message:active.Notice;
            metricValues[0].Text=active.HasFrame?active.Frame.ToString(CultureInfo.InvariantCulture):"--";
            metricValues[1].Text=active.HasArm?active.Samples+" 帧（不等于票数）":"--";
            metricValues[2].Text=active.InferenceMs.HasValue?active.InferenceMs.Value.ToString("F2",CultureInfo.InvariantCulture)+" ms / 周期日志":"--";
            metricValues[3].Text=fresh?control.Votes[0]+" / "+control.Votes[1]+" / "+control.Votes[2]+" / "+control.Votes[3]+"  弃权"+control.Uncertain:"旧ELF/离线：无实时票数";metricValues[3].ForeColor=fresh?Theme.Accent:Theme.Muted;
            metricValues[4].Text="--";metricValues[5].Text="等待 ARM5 配套状态";
            if(fresh){
                stateLabel.Text=control.Enabled&&control.Busy&&control.State==0?"正在回初始姿态":MonitorModel.States[control.State];
                stateLabel.ForeColor=control.State==5?Theme.Red:Theme.Text;
                stateDetail.Text=control.State==3||control.State==4?"本轮分类："+MonitorModel.Classes[control.Result]:control.State==5?MonitorModel.Reasons[control.Reason]:control.Enabled?"PWM已使能；等待新的单次触发。":"PWM已失能；使能后可能立即回初始姿态。";
                metricValues[0].Text=control.Frame.ToString(CultureInfo.InvariantCulture);metricValues[1].Text=control.Samples+" 帧 / 窗口"+control.Window;
                if(control.WorkflowFresh(DateTime.UtcNow)) {
                    stateLabel.Text=control.Phase==0 && control.LocateOnly && control.CoordinatesValid?"定位完成 · 等待操作":control.PhaseName;
                    stateDetail.Text=control.Phase==7?MonitorModel.Reasons[control.Reason]:
                        control.Phase==5?"正在采样投票；本轮类别尚未确认。":
                        control.Phase==6?"本轮分类："+MonitorModel.Classes[control.Result]+"；正在执行分类投放":
                        control.Phase==0?"单次运行；动作期间不接受重复触发。":"观察定位 → 到达圆片上方 → 分类抓取 → 返回";
                    metricValues[4].Text=control.CoordinatesValid?"X="+control.Xmm+"  Y="+control.Ymm+" mm":"本轮坐标尚未有效";
                    metricValues[4].ForeColor=control.CoordinatesValid?Theme.Accent:Theme.Muted;
                    metricValues[5].Text=(control.ObservationTaught?"已记录":"未记录")+" / "+(control.Calibrated?"已加载":"无效");
                }
            }
            else if(connected && control.HasState){stateLabel.Text="状态过期 / 控制锁定";stateLabel.ForeColor=Theme.Amber;stateDetail.Text="当前机械臂状态未知；请检查CPU暂停、程序或串口，不要盲目重发。";}
            summary.Text="已观察完成 "+active.Completed+" 次 | 异常轮次 "+active.Errors+" | 其他日志行 "+active.IgnoredLines;
            footer.Text=(isDemo?"【演示数据，不代表实测】 ":isReplay?"【离线日志回放】 ":"")+"HDMI显示视频 | 100×100毫米内框坐标 | 单次定位识别抓取分拣 | 动作完成≠抓取成功 | 失能不是急停";
            var first=active.Records.Count>0?active.Records[0]:null;
            if(renderedModel!=active || renderedFirst!=first)
            {
                history.Rows.Clear();int n=0;
                foreach(CycleRecord item in active.Records){if(n++>=200)break;
                    int index=history.Rows.Add(item.At.HasValue?item.At.Value.ToString("HH:mm:ss"):"无时间戳",item.Category,item.Outcome,
                        item.Observation+" / "+item.EndFrame,item.Samples,item.Seconds.HasValue?item.Seconds.Value.ToString("F1"):"--");
                    history.Rows[index].Cells[1].Style.ForeColor=item.Category=="未确认"?Theme.Amber:Theme.Accent;
                }
                history.ClearSelection();renderedFirst=first;renderedModel=active;
            }
            targets.Rows.Clear();foreach(Detection item in active.Detections)targets.Rows.Add(item.Frame,item.Id,MonitorModel.Classes[item.ClassId],(item.ScorePpm/10000.0).ToString("F2")+"%",item.X1+","+item.Y1+" → "+item.X2+","+item.Y2);targets.ClearSelection();
        }

        void Export()
        {
            using(var dialog=new SaveFileDialog {Filter="CSV 表格|*.csv",FileName=(isDemo?"DEMO_":isReplay?"REPLAY_":"")+"分拣记录_"+DateTime.Now.ToString("yyyyMMdd_HHmmss")+".csv",InitialDirectory=root})
            {
                if(dialog.ShowDialog(this)!=DialogResult.OK)return;
                try {File.WriteAllText(dialog.FileName,active.ExportCsv(),new UTF8Encoding(true));MessageBox.Show(this,"记录已导出。数据来源与计数限制已写入表格。","导出完成");}
                catch(Exception ex){MessageBox.Show(this,ex.Message,"导出失败");}
            }
        }

        void Replay()
        {
            using(var dialog=new OpenFileDialog {Filter="串口日志|*.log;*.txt|所有文件|*.*",InitialDirectory=root})
            {
                if(dialog.ShowDialog(this)!=DialogResult.OK)return;
                try {
                    var info=new FileInfo(dialog.FileName);if(info.Length>50*1024*1024)throw new IOException("日志超过50MB，请先切分。");
                    CloseSerial(true);demoTimer.Stop();isDemo=false;isReplay=true;active=new MonitorModel("离线回放");log.Clear();
                    foreach(string raw in File.ReadLines(dialog.FileName))
                    {
                        string line;DateTime? at;MonitorModel.UnpackRecordedLine(raw,out line,out at);
                        active.Process(line,at);if(raw.Length<8193)AppendLog(line);
                    }
                    active.Notice="离线回放结束。无时间戳的日志不生成耗时；缺失流程不会补算。";renderDirty=true;RefreshView();
                } catch(Exception ex){MessageBox.Show(this,ex.Message,"回放失败");}
            }
        }
        void LeaveOffline(){demoTimer.Stop();isDemo=false;isReplay=false;active=live;renderDirty=true;}
        void ResetStatistics()
        {
            if(MessageBox.Show(this,"只清空当前电脑界面的统计，不会发送串口命令，也不会清空开发板状态。\n若当前正在动作，该轮不会计入新统计。是否继续？","清空本机统计",MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;
            if(!isDemo&&!isReplay){
                SaveEvents();
                if(csvPath!=null)csvPath=Path.Combine(Path.GetDirectoryName(csvPath),"reset_"+DateTime.Now.ToString("yyyyMMdd_HHmmss_fff",CultureInfo.InvariantCulture)+"_events.csv");
                live=new MonitorModel("实时串口 / 本机观察");active=live;
                live.Notice="本机统计已清空；此前日志文件保留。当前正在进行的流程不补算。";
            }
            else active=new MonitorModel(isDemo?"演示数据":"离线回放");renderDirty=true;RefreshView();
        }

        static string ArmLine(int state,int cls,int samples,uint frame,int reason)
        {return "ARM_SORT state="+state+" result="+cls+" samples="+samples+" frame="+frame+" reason="+reason+" mixed=0";}
        public void EnterDemo()
        {
            CloseSerial(true);isDemo=true;isReplay=false;active=new MonitorModel("演示数据 / 非实测");demoStep=0;log.Clear();
            SeedPreview();renderDirty=true;RefreshView();demoTimer.Start();
        }
        void SeedPreview()
        {
            DateTime start=DateTime.Now.AddMinutes(-2);
            int[] states={1,2,3,4,0};
            for(int c=0;c<4;c++)for(int s=0;s<5;s++)active.Process(ArmLine(states[s],c,s<2?0:5,(uint)(10+c*10+s),0),start.AddSeconds(c*20+s*2));
            active.Process("YOLO inference: 295488 us",DateTime.Now);
            active.Process("DEFECT frame=60 id=4 class=scratch score_ppm=992351 box=334,260,577,433",DateTime.Now);
            AppendLog("[演示] 以下是模拟日志，不是开发板数据。");AppendLog("YOLO inference: 295488 us");AppendLog(ArmLine(0,3,5,44,0));
        }
        void DemoNext()
        {
            int[] states={1,2,3,4,0};int index=demoStep%5,cls=(demoStep/5)%4;
            string line=ArmLine(states[index],cls,index<2?0:5,(uint)(100+demoStep),0);active.Process(line,DateTime.Now);AppendLog("[演示] "+line);
            demoStep++;renderDirty=true;RefreshView();
        }
        public void SavePreview(string path,bool simulated,bool compact)
        {
            timer.Stop();demoTimer.Stop();isDemo=simulated;
            active=simulated?new MonitorModel("演示数据 / 非实测"):live;
            if(simulated)SeedPreview();if(compact)ClientSize=new Size(1130,820);renderDirty=true;
            // Make only this app's test window visible to WinForms, far off screen.
            // A hidden top-level form otherwise suppresses every child in DrawToBitmap.
            StartPosition=FormStartPosition.Manual;Location=new Point(-20000,-20000);ShowInTaskbar=false;
            Show();Application.DoEvents();RefreshView();PerformLayout();Update();Application.DoEvents();
            if(simulated){
                stateLabel.Text="定位完成 · 等待操作";
                stateDetail.Text="演示圆心；未连接设备，不是实板定位结果。";
                metricValues[4].Text="X=50  Y=54 mm";metricValues[4].ForeColor=Theme.Accent;
                metricValues[5].Text="已记录 / 已加载（演示）";
            }
            using(var image=new Bitmap(Width,Height)){DrawToBitmap(image,new Rectangle(0,0,Width,Height));image.Save(path,System.Drawing.Imaging.ImageFormat.Png);}
            Hide();
        }
    }

    static class Program
    {
        [STAThread]
        static int Main(string[] args)
        {
            Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
            Application.ThreadException+=delegate(object sender,ThreadExceptionEventArgs e){MessageBox.Show("界面异常："+e.Exception.Message+"\n未向开发板发送任何恢复命令。","上位机错误");};
            try {
                using(var form=new MainForm())
                {
                    if(args.Length==2 && (args[0]=="--preview" || args[0]=="--preview-idle" || args[0]=="--preview-compact")) {
                        form.SavePreview(Path.GetFullPath(args[1]),args[0]!="--preview-idle",args[0]=="--preview-compact");return 0;
                    }
                    if(args.Length==1 && args[0]=="--demo")form.EnterDemo();
                    Application.Run(form);return 0;
                }
            } catch(Exception ex){MessageBox.Show(ex.ToString(),"上位机启动失败");return 1;}
        }
    }
}
