// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class StopRequest : Table {
  public static StopRequest GetRootAsStopRequest(ByteBuffer _bb) { return GetRootAsStopRequest(_bb, new StopRequest()); }
  public static StopRequest GetRootAsStopRequest(ByteBuffer _bb, StopRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public StopRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartStopRequest(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndStopRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
