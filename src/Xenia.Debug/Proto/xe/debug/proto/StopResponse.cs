// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class StopResponse : Table {
  public static StopResponse GetRootAsStopResponse(ByteBuffer _bb) { return GetRootAsStopResponse(_bb, new StopResponse()); }
  public static StopResponse GetRootAsStopResponse(ByteBuffer _bb, StopResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public StopResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartStopResponse(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndStopResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
