// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class Request : Table {
  public static Request GetRootAsRequest(ByteBuffer _bb) { return GetRootAsRequest(_bb, new Request()); }
  public static Request GetRootAsRequest(ByteBuffer _bb, Request obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public Request __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public uint Id { get { int o = __offset(4); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public RequestData RequestDataType { get { int o = __offset(6); return o != 0 ? (RequestData)bb.Get(o + bb_pos) : (RequestData)0; } }
  public Table GetRequestData(Table obj) { int o = __offset(8); return o != 0 ? __union(obj, o) : null; }

  public static int CreateRequest(FlatBufferBuilder builder,
      uint id = 0,
      RequestData request_data_type = 0,
      int request_data = 0) {
    builder.StartObject(3);
    Request.AddRequestData(builder, request_data);
    Request.AddId(builder, id);
    Request.AddRequestDataType(builder, request_data_type);
    return Request.EndRequest(builder);
  }

  public static void StartRequest(FlatBufferBuilder builder) { builder.StartObject(3); }
  public static void AddId(FlatBufferBuilder builder, uint id) { builder.AddUint(0, id, 0); }
  public static void AddRequestDataType(FlatBufferBuilder builder, RequestData requestDataType) { builder.AddByte(1, (byte)(requestDataType), 0); }
  public static void AddRequestData(FlatBufferBuilder builder, int requestDataOffset) { builder.AddOffset(2, requestDataOffset, 0); }
  public static int EndRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
