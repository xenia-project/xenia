// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class ListThreadsRequest : Table {
  public static ListThreadsRequest GetRootAsListThreadsRequest(ByteBuffer _bb) { return GetRootAsListThreadsRequest(_bb, new ListThreadsRequest()); }
  public static ListThreadsRequest GetRootAsListThreadsRequest(ByteBuffer _bb, ListThreadsRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public ListThreadsRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartListThreadsRequest(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndListThreadsRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
