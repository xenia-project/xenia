// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class AttachRequest : Table {
  public static AttachRequest GetRootAsAttachRequest(ByteBuffer _bb) { return GetRootAsAttachRequest(_bb, new AttachRequest()); }
  public static AttachRequest GetRootAsAttachRequest(ByteBuffer _bb, AttachRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public AttachRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartAttachRequest(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndAttachRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
