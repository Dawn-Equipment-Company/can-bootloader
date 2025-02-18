using Peak.Can.Basic;
using System.Diagnostics;
using System.Linq;
using System.Net.Sockets;
using System.Text;

namespace EcuUpdater
{
    // scale address = 0x77
    public partial class Form1 : Form
    {
        public int addr = 0x77;
        public Form1()
        {
            InitializeComponent();
        }

        private void button1_Click(object sender, EventArgs e)
        {
            var tokenSource = new CancellationTokenSource();
            var cancel = tokenSource.Token;

            var t = Task.Run(() =>
            {
                var status = Peak.Can.Basic.Api.Initialize(PcanChannel.Usb01, Bitrate.Pcan250);
                Debug.WriteLine(string.Format("Init status: {0}", status));
                button2.Invoke(() => button2.Enabled = true);

                var msg = default(PcanMessage);
                while (true)
                {
                    if (cancel.IsCancellationRequested)
                    {
                        return;
                    }
                    while (Api.Read(PcanChannel.Usb01, out msg) == PcanStatus.OK)
                    {
                        //Debug.WriteLine("header: {0}", msg.ID);
                        //HandleMessage(msg);
                    }
                    System.Threading.Thread.Sleep(100);
                }
            }, tokenSource.Token);
        }

        private void PcanSend(CanMessage msg)
        {
            PcanMessage tx_msg = new PcanMessage();
            tx_msg.MsgType = MessageType.Extended;
            tx_msg.ID = (uint)msg.Header;
            tx_msg.DLC = (byte)msg.Data.Length;
            tx_msg.Data = msg.Data;
            Api.Write(PcanChannel.Usb01, tx_msg);
        }

        private CanMessage? PcanRead()
        {
            PcanMessage rx_msg = new PcanMessage();
            if (Api.Read(PcanChannel.Usb01, out rx_msg) == PcanStatus.OK)
            {
                return new CanMessage()
                {
                    Header = (int)rx_msg.ID,
                    Data = rx_msg.Data,
                };
            }
            else
            {
                return null;
            }
        }

        private void button2_Click(object sender, EventArgs e)
        {
            //PcanMessage tx_msg = new PcanMessage();
            //tx_msg.MsgType = MessageType.Extended;
            //tx_msg.ID = 0x18E32099;
            //tx_msg.DLC = 0;
            if (openFileDialog1.ShowDialog() == DialogResult.OK)
            {
                // start bootload message - needs more info
                //tx_msg.ID = 0x18E32099;
                //Api.Write(PcanChannel.Usb01, tx_msg);
                //System.Threading.Thread.Sleep(200);
                PcanSend(new CanMessage()
                {
                    Header = 0x18E30099 | (addr << 8),
                    Data = new byte[0],
                });

                var fileName = openFileDialog1.FileName;
                using (var stream = File.Open(fileName, FileMode.Open))
                {
                    var segmentCount = 1 + (stream.Length / 980);
                    progressBar1.Maximum = (int)segmentCount;
                    Debug.WriteLine("Have {0} segments to send", segmentCount);
                    using (var reader = new BinaryReader(stream))
                    {
                        for (var currentSegment = 0; currentSegment < segmentCount; currentSegment++)
                        {
                            Debug.WriteLine(string.Format("TX seg {0} / {1}", currentSegment, segmentCount));
                            // break the overall file into 980byte (7*140) segments
                            // so that segmnents fit into TP sessions and data on
                            // the remote end fits into a 1024 byte buffer
                            var segment_bytes = new byte[980];
                            var offset = currentSegment * 980;
                            segment_bytes = reader.ReadBytes(980);
                            //reader.Read(segment_bytes, offset, 980);

                            var t = new Transfer(segment_bytes, addr);
                            t.Start();
                            var tx = t.Next(null);
                            PcanSend(tx.First());
                            var sent_data = false;
                            while (!sent_data)
                            {
                                // need to wait for CTS here
                                var tx_data = t.Next(PcanRead());
                                foreach (var tt in tx_data)
                                {
                                    PcanSend(tt);
                                    System.Threading.Thread.Sleep(1);
                                }
                                if (tx_data.Count > 0)
                                {
                                    sent_data = true;
                                    // send end of segment message
                                    PcanSend(new CanMessage()
                                    {
                                        Header = 0x18E40099 | (addr << 8),
                                        Data = new byte[] { 0 },
                                    });
                                }
                            }
                            progressBar1.Value++;
                        }
                    }
                }
                Debug.WriteLine("Done");
                PcanSend(new CanMessage()
                {
                    Header = 0x18E50099 | (addr << 8),
                    Data = new byte[0],
                });
            }
        }
    }
}