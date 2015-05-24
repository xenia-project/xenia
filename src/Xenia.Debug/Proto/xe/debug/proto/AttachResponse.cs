// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class AttachResponse : Table {
  public static AttachResponse GetRootAsAttachResponse(ByteBuffer _bb) { return GetRootAsAttachResponse(_bb, new AttachResponse()); }
  public static AttachResponse GetRootAsAttachResponse(ByteBuffer _bb, AttachResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public AttachResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartAttachResponse(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndAttachResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
