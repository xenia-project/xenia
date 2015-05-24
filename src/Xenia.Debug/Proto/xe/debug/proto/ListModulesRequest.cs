// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class ListModulesRequest : Table {
  public static ListModulesRequest GetRootAsListModulesRequest(ByteBuffer _bb) { return GetRootAsListModulesRequest(_bb, new ListModulesRequest()); }
  public static ListModulesRequest GetRootAsListModulesRequest(ByteBuffer _bb, ListModulesRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public ListModulesRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartListModulesRequest(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndListModulesRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
