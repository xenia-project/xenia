#include <idc.idc>

static main()
{
  auto just_code;
  auto seg_start, seg_end, seg_base, seg_name, seg_type;
  auto item_start, item_end, item_flags, item_name;
  auto path, handle;

  seg_start = FirstSeg();
  seg_end = SegEnd(seg_start);
  if (seg_start == BADADDR ||
      seg_end == BADADDR)
  {
    return;
  }

  path = AskFile(1, "*.map", "Save map");
  if (path == "")
  {
    return;
  }


  just_code = AskYN(0, "Just code?") == 1;

  handle = fopen(path, "wb");
  if (!handle)
  {
    return;
  }

  seg_base = 0;
  fprintf(handle, " Start         Length     Name                   Class\n");
  do
  {
    seg_base++;
    seg_name = SegName(seg_start);
    seg_type = GetSegmentAttr(seg_start, SEGATTR_TYPE);

    fprintf(handle, " %04x:%08x %08xH %-23s %s\n", seg_base, -1, seg_end - seg_start, seg_name, seg_type == 2 ? "CODE" : "DATA");

    seg_start = NextSeg(seg_start);
    seg_end = SegEnd(seg_start);
  }
  while (seg_start != BADADDR &&
         seg_end != BADADDR);

  fprintf(handle, "\n");
  fprintf(handle, "  Address         Publics by Value              Rva+Base       Lib:Object\n");

  seg_start = FirstSeg();
  seg_end = SegEnd(seg_start);
  seg_base = 0;
  do
  {
    seg_base++;

    item_start = NextHead(seg_start, seg_end);
    while (item_start != BADADDR)
    {
      item_end = ItemEnd(item_start);
      if (item_end == BADADDR)
      {
        break;
      }

      item_name = GetTrueNameEx(BADADDR, item_start);
      if (item_name != "" &&
          substr(item_name, 0, 4) != "loc_" &&
          substr(item_name, 0, 4) != "sub_" &&
          substr(item_name, 0, 4) != "off_" &&
          substr(item_name, 0, 4) != "flt_" &&
          substr(item_name, 0, 5) != "byte_" &&
          substr(item_name, 0, 5) != "word_" &&
          substr(item_name, 0, 6) != "dword_" &&
          substr(item_name, 0, 6) != "qword_")
      {
        item_flags = GetFlags(item_start);
        if (just_code == 0 || (item_flags & FF_CODE) == FF_CODE)
        {
          fprintf(handle, " %04x:%08x    %-29s %08x       %s\n", seg_base, item_start - seg_start, item_name, item_start, "<???>");
        }
      }
      item_start = NextHead(item_end, seg_end);
    }

    seg_start = NextSeg(seg_start);
    seg_end = SegEnd(seg_start);
  }
  while (seg_start != BADADDR &&
         seg_end != BADADDR);

  fclose(handle);
}
