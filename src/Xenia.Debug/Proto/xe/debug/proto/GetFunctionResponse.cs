// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class GetFunctionResponse : Table {
  public static GetFunctionResponse GetRootAsGetFunctionResponse(ByteBuffer _bb) { return GetRootAsGetFunctionResponse(_bb, new GetFunctionResponse()); }
  public static GetFunctionResponse GetRootAsGetFunctionResponse(ByteBuffer _bb, GetFunctionResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public GetFunctionResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public Function Function { get { return GetFunction(new Function()); } }
  public Function GetFunction(Function obj) { int o = __offset(4); return o != 0 ? obj.__init(__indirect(o + bb_pos), bb) : null; }

  public static int CreateGetFunctionResponse(FlatBufferBuilder builder,
      int function = 0) {
    builder.StartObject(1);
    GetFunctionResponse.AddFunction(builder, function);
    return GetFunctionResponse.EndGetFunctionResponse(builder);
  }

  public static void StartGetFunctionResponse(FlatBufferBuilder builder) { builder.StartObject(1); }
  public static void AddFunction(FlatBufferBuilder builder, int functionOffset) { builder.AddOffset(0, functionOffset, 0); }
  public static int EndGetFunctionResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
