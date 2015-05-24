// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class ContinueRequest : Table {
  public static ContinueRequest GetRootAsContinueRequest(ByteBuffer _bb) { return GetRootAsContinueRequest(_bb, new ContinueRequest()); }
  public static ContinueRequest GetRootAsContinueRequest(ByteBuffer _bb, ContinueRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public ContinueRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public ContinueAction Action { get { int o = __offset(4); return o != 0 ? (ContinueAction)bb.GetSbyte(o + bb_pos) : (ContinueAction)0; } }
  public uint TargetAddress { get { int o = __offset(6); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }

  public static int CreateContinueRequest(FlatBufferBuilder builder,
      ContinueAction action = 0,
      uint target_address = 0) {
    builder.StartObject(2);
    ContinueRequest.AddTargetAddress(builder, target_address);
    ContinueRequest.AddAction(builder, action);
    return ContinueRequest.EndContinueRequest(builder);
  }

  public static void StartContinueRequest(FlatBufferBuilder builder) { builder.StartObject(2); }
  public static void AddAction(FlatBufferBuilder builder, ContinueAction action) { builder.AddSbyte(0, (sbyte)(action), 0); }
  public static void AddTargetAddress(FlatBufferBuilder builder, uint targetAddress) { builder.AddUint(1, targetAddress, 0); }
  public static int EndContinueRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
