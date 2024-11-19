using Microsoft.VisualBasic.ApplicationServices;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EcuUpdater
{
    public class CanMessage
    {
        public int Header { get; set; }
        public byte[] Data { get; set; }
    }

    public class Transfer
    {
        enum TransferState
        {
            Idle,
            SendingRts,
            SentRts,
            SentData,
        }

        public Transfer(byte[] stream)
        {
            this._data = stream;
            this._byteCount = (int)stream.Length;
            this._packetCount = this._byteCount / 7;
        }

        public void Start()
        {
            this._state = TransferState.SendingRts;
        }

        public IList<CanMessage> Next(CanMessage? msg)
        {
            var rval = new List<CanMessage>();
            if(this._state == TransferState.SendingRts)
            {
                var b1 = (byte)((this._byteCount & 0xFF00) >> 8);
                var b2 = (byte)(this._byteCount & 0x00FF);
                rval.Add(new CanMessage()
                {
                    Header = 0x18EC7799,
                    Data = new byte[] { 16, b2, b1, (byte)this._packetCount, 0xFF, 0, 0, 0 }
                });
                this._state = TransferState.SentRts;
            }
            else if(this._state == TransferState.SentRts)
            {
                if ((msg != null) && ((msg.Header & 0x00FFFF00) == 0x00EC9900))
                {
                    if (msg.Data[0] != 19)
                    {
                        Debug.WriteLine("Expecting RTS got something else");
                    }
                    else
                    {
                        var short_message = false;
                        var remainder = this._byteCount - (this._packetCount * 7);
                        if (this._byteCount % 7 != 0)
                        {
                            Debug.WriteLine("have data that doesn't fit boundary");
                            Debug.WriteLine("remainder: {0} bytes", remainder);
                            short_message = true;
                        }
                        var last_i = 0;
                        // packetize this segment and return all data transfer messages
                        for (var i = 0; i < this._byteCount / 7; i++)
                        {
                            var m = new CanMessage()
                            {
                                Header = 0x18EB7799,
                                Data = new[] {
                                    (byte)i,
                                    this._data[i * 7],
                                    this._data[i * 7 + 1],
                                    this._data[i * 7 + 2],
                                    this._data[i * 7 + 3],
                                    this._data[i * 7 + 4],
                                    this._data[i * 7 + 5],
                                    this._data[i * 7 + 6],
                                }
                            };
                            rval.Add(m);
                            last_i++;
                        }
                        if(short_message)
                        {
                            var i = (last_i) * 7;
                            var rd = new byte[remainder+1];
                            rd[0] = (byte)last_i;
                            for(var x = 0; x < remainder; x++)
                            {
                                rd[x+1] = this._data[i + x];
                            }
                            var m = new CanMessage()
                            {
                                Header = 0x18EB7799,
                                Data = rd,
                            };
                            rval.Add(m);
                        }
                        this._state = TransferState.SentData;
                    }
                }
            }

            return rval;

        }

        private byte[] _data;
        private TransferState _state;
        private int _byteCount;
        private int _packetCount;
    }
}
