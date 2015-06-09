// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class GetFunctionRequest : Table {
  public static GetFunctionRequest GetRootAsGetFunctionRequest(ByteBuffer _bb) { return GetRootAsGetFunctionRequest(_bb, new GetFunctionRequest()); }
  public static GetFunctionRequest GetRootAsGetFunctionRequest(ByteBuffer _bb, GetFunctionRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public GetFunctionRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public ulong Identifier { get { int o = __offset(4); return o != 0 ? bb.GetUlong(o + bb_pos) : (ulong)0; } }

  public static int CreateGetFunctionRequest(FlatBufferBuilder builder,
      ulong identifier = 0) {
    builder.StartObject(1);
    GetFunctionRequest.AddIdentifier(builder, identifier);
    return GetFunctionRequest.EndGetFunctionRequest(builder);
  }

  public static void StartGetFunctionRequest(FlatBufferBuilder builder) { builder.StartObject(1); }
  public static void AddIdentifier(FlatBufferBuilder builder, ulong identifier) { builder.AddUlong(0, identifier, 0); }
  public static int EndGetFunctionRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
