// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class ContinueResponse : Table {
  public static ContinueResponse GetRootAsContinueResponse(ByteBuffer _bb) { return GetRootAsContinueResponse(_bb, new ContinueResponse()); }
  public static ContinueResponse GetRootAsContinueResponse(ByteBuffer _bb, ContinueResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public ContinueResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartContinueResponse(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndContinueResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
