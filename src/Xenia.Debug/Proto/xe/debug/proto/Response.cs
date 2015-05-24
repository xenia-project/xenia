// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class Response : Table {
  public static Response GetRootAsResponse(ByteBuffer _bb) { return GetRootAsResponse(_bb, new Response()); }
  public static Response GetRootAsResponse(ByteBuffer _bb, Response obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public Response __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public uint Id { get { int o = __offset(4); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public ResponseData ResponseDataType { get { int o = __offset(6); return o != 0 ? (ResponseData)bb.Get(o + bb_pos) : (ResponseData)0; } }
  public Table GetResponseData(Table obj) { int o = __offset(8); return o != 0 ? __union(obj, o) : null; }

  public static int CreateResponse(FlatBufferBuilder builder,
      uint id = 0,
      ResponseData response_data_type = 0,
      int response_data = 0) {
    builder.StartObject(3);
    Response.AddResponseData(builder, response_data);
    Response.AddId(builder, id);
    Response.AddResponseDataType(builder, response_data_type);
    return Response.EndResponse(builder);
  }

  public static void StartResponse(FlatBufferBuilder builder) { builder.StartObject(3); }
  public static void AddId(FlatBufferBuilder builder, uint id) { builder.AddUint(0, id, 0); }
  public static void AddResponseDataType(FlatBufferBuilder builder, ResponseData responseDataType) { builder.AddByte(1, (byte)(responseDataType), 0); }
  public static void AddResponseData(FlatBufferBuilder builder, int responseDataOffset) { builder.AddOffset(2, responseDataOffset, 0); }
  public static int EndResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
