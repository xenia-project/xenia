// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class Thread : Table {
  public static Thread GetRootAsThread(ByteBuffer _bb) { return GetRootAsThread(_bb, new Thread()); }
  public static Thread GetRootAsThread(ByteBuffer _bb, Thread obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public Thread __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public XObject Object { get { return GetObject(new XObject()); } }
  public XObject GetObject(XObject obj) { int o = __offset(4); return o != 0 ? obj.__init(o + bb_pos, bb) : null; }
  public ThreadType Type { get { int o = __offset(6); return o != 0 ? (ThreadType)bb.GetSbyte(o + bb_pos) : (ThreadType)0; } }

  public static void StartThread(FlatBufferBuilder builder) { builder.StartObject(2); }
  public static void AddObject(FlatBufferBuilder builder, int objectOffset) { builder.AddStruct(0, objectOffset, 0); }
  public static void AddType(FlatBufferBuilder builder, ThreadType type) { builder.AddSbyte(1, (sbyte)(type), 0); }
  public static int EndThread(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
