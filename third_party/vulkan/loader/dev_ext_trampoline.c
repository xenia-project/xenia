/*
 * Copyright (c) 2015-2016 The Khronos Group Inc.
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Jon Ashburn <jon@lunarg.com>
 */

#include "vk_loader_platform.h"
#include "loader.h"
#if defined(__linux__)
#pragma GCC optimize(3) // force gcc to use tail-calls
#endif

VKAPI_ATTR void VKAPI_CALL vkdev_ext0(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[0](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext1(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[1](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext2(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[2](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext3(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[3](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext4(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[4](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext5(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[5](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext6(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[6](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext7(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[7](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext8(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[8](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext9(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[9](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext10(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[10](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext11(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[11](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext12(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[12](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext13(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[13](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext14(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[14](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext15(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[15](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext16(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[16](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext17(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[17](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext18(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[18](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext19(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[19](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext20(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[20](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext21(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[21](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext22(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[22](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext23(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[23](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext24(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[24](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext25(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[25](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext26(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[26](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext27(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[27](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext28(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[28](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext29(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[29](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext30(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[30](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext31(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[31](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext32(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[32](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext33(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[33](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext34(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[34](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext35(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[35](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext36(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[36](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext37(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[37](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext38(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[38](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext39(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[39](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext40(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[40](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext41(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[41](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext42(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[42](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext43(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[43](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext44(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[44](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext45(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[45](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext46(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[46](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext47(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[47](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext48(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[48](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext49(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[49](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext50(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[50](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext51(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[51](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext52(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[52](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext53(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[53](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext54(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[54](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext55(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[55](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext56(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[56](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext57(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[57](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext58(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[58](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext59(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[59](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext60(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[60](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext61(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[61](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext62(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[62](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext63(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[63](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext64(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[64](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext65(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[65](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext66(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[66](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext67(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[67](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext68(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[68](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext69(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[69](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext70(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[70](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext71(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[71](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext72(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[72](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext73(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[73](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext74(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[74](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext75(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[75](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext76(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[76](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext77(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[77](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext78(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[78](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext79(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[79](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext80(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[80](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext81(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[81](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext82(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[82](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext83(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[83](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext84(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[84](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext85(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[85](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext86(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[86](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext87(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[87](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext88(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[88](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext89(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[89](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext90(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[90](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext91(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[91](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext92(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[92](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext93(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[93](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext94(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[94](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext95(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[95](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext96(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[96](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext97(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[97](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext98(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[98](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext99(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[99](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext100(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[100](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext101(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[101](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext102(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[102](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext103(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[103](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext104(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[104](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext105(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[105](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext106(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[106](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext107(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[107](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext108(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[108](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext109(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[109](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext110(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[110](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext111(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[111](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext112(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[112](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext113(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[113](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext114(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[114](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext115(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[115](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext116(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[116](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext117(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[117](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext118(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[118](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext119(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[119](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext120(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[120](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext121(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[121](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext122(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[122](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext123(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[123](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext124(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[124](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext125(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[125](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext126(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[126](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext127(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[127](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext128(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[128](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext129(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[129](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext130(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[130](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext131(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[131](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext132(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[132](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext133(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[133](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext134(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[134](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext135(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[135](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext136(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[136](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext137(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[137](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext138(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[138](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext139(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[139](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext140(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[140](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext141(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[141](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext142(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[142](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext143(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[143](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext144(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[144](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext145(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[145](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext146(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[146](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext147(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[147](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext148(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[148](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext149(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[149](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext150(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[150](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext151(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[151](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext152(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[152](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext153(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[153](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext154(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[154](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext155(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[155](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext156(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[156](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext157(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[157](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext158(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[158](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext159(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[159](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext160(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[160](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext161(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[161](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext162(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[162](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext163(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[163](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext164(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[164](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext165(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[165](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext166(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[166](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext167(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[167](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext168(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[168](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext169(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[169](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext170(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[170](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext171(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[171](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext172(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[172](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext173(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[173](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext174(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[174](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext175(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[175](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext176(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[176](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext177(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[177](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext178(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[178](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext179(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[179](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext180(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[180](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext181(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[181](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext182(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[182](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext183(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[183](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext184(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[184](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext185(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[185](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext186(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[186](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext187(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[187](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext188(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[188](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext189(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[189](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext190(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[190](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext191(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[191](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext192(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[192](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext193(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[193](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext194(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[194](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext195(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[195](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext196(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[196](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext197(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[197](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext198(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[198](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext199(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[199](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext200(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[200](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext201(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[201](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext202(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[202](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext203(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[203](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext204(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[204](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext205(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[205](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext206(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[206](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext207(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[207](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext208(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[208](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext209(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[209](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext210(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[210](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext211(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[211](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext212(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[212](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext213(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[213](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext214(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[214](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext215(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[215](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext216(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[216](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext217(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[217](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext218(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[218](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext219(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[219](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext220(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[220](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext221(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[221](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext222(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[222](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext223(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[223](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext224(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[224](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext225(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[225](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext226(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[226](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext227(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[227](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext228(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[228](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext229(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[229](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext230(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[230](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext231(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[231](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext232(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[232](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext233(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[233](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext234(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[234](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext235(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[235](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext236(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[236](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext237(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[237](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext238(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[238](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext239(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[239](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext240(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[240](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext241(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[241](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext242(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[242](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext243(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[243](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext244(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[244](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext245(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[245](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext246(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[246](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext247(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[247](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext248(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[248](device);
}

VKAPI_ATTR void VKAPI_CALL vkdev_ext249(VkDevice device) {
    const struct loader_dev_dispatch_table *disp;
    disp = loader_get_dev_dispatch(device);
    disp->ext_dispatch.dev_ext[249](device);
}

void *loader_get_dev_ext_trampoline(uint32_t index) {
    switch (index) {
    case 0:
        return vkdev_ext0;
    case 1:
        return vkdev_ext1;
    case 2:
        return vkdev_ext2;
    case 3:
        return vkdev_ext3;
    case 4:
        return vkdev_ext4;
    case 5:
        return vkdev_ext5;
    case 6:
        return vkdev_ext6;
    case 7:
        return vkdev_ext7;
    case 8:
        return vkdev_ext8;
    case 9:
        return vkdev_ext9;
    case 10:
        return vkdev_ext10;
    case 11:
        return vkdev_ext11;
    case 12:
        return vkdev_ext12;
    case 13:
        return vkdev_ext13;
    case 14:
        return vkdev_ext14;
    case 15:
        return vkdev_ext15;
    case 16:
        return vkdev_ext16;
    case 17:
        return vkdev_ext17;
    case 18:
        return vkdev_ext18;
    case 19:
        return vkdev_ext19;
    case 20:
        return vkdev_ext20;
    case 21:
        return vkdev_ext21;
    case 22:
        return vkdev_ext22;
    case 23:
        return vkdev_ext23;
    case 24:
        return vkdev_ext24;
    case 25:
        return vkdev_ext25;
    case 26:
        return vkdev_ext26;
    case 27:
        return vkdev_ext27;
    case 28:
        return vkdev_ext28;
    case 29:
        return vkdev_ext29;
    case 30:
        return vkdev_ext30;
    case 31:
        return vkdev_ext31;
    case 32:
        return vkdev_ext32;
    case 33:
        return vkdev_ext33;
    case 34:
        return vkdev_ext34;
    case 35:
        return vkdev_ext35;
    case 36:
        return vkdev_ext36;
    case 37:
        return vkdev_ext37;
    case 38:
        return vkdev_ext38;
    case 39:
        return vkdev_ext39;
    case 40:
        return vkdev_ext40;
    case 41:
        return vkdev_ext41;
    case 42:
        return vkdev_ext42;
    case 43:
        return vkdev_ext43;
    case 44:
        return vkdev_ext44;
    case 45:
        return vkdev_ext45;
    case 46:
        return vkdev_ext46;
    case 47:
        return vkdev_ext47;
    case 48:
        return vkdev_ext48;
    case 49:
        return vkdev_ext49;
    case 50:
        return vkdev_ext50;
    case 51:
        return vkdev_ext51;
    case 52:
        return vkdev_ext52;
    case 53:
        return vkdev_ext53;
    case 54:
        return vkdev_ext54;
    case 55:
        return vkdev_ext55;
    case 56:
        return vkdev_ext56;
    case 57:
        return vkdev_ext57;
    case 58:
        return vkdev_ext58;
    case 59:
        return vkdev_ext59;
    case 60:
        return vkdev_ext60;
    case 61:
        return vkdev_ext61;
    case 62:
        return vkdev_ext62;
    case 63:
        return vkdev_ext63;
    case 64:
        return vkdev_ext64;
    case 65:
        return vkdev_ext65;
    case 66:
        return vkdev_ext66;
    case 67:
        return vkdev_ext67;
    case 68:
        return vkdev_ext68;
    case 69:
        return vkdev_ext69;
    case 70:
        return vkdev_ext70;
    case 71:
        return vkdev_ext71;
    case 72:
        return vkdev_ext72;
    case 73:
        return vkdev_ext73;
    case 74:
        return vkdev_ext74;
    case 75:
        return vkdev_ext75;
    case 76:
        return vkdev_ext76;
    case 77:
        return vkdev_ext77;
    case 78:
        return vkdev_ext78;
    case 79:
        return vkdev_ext79;
    case 80:
        return vkdev_ext80;
    case 81:
        return vkdev_ext81;
    case 82:
        return vkdev_ext82;
    case 83:
        return vkdev_ext83;
    case 84:
        return vkdev_ext84;
    case 85:
        return vkdev_ext85;
    case 86:
        return vkdev_ext86;
    case 87:
        return vkdev_ext87;
    case 88:
        return vkdev_ext88;
    case 89:
        return vkdev_ext89;
    case 90:
        return vkdev_ext90;
    case 91:
        return vkdev_ext91;
    case 92:
        return vkdev_ext92;
    case 93:
        return vkdev_ext93;
    case 94:
        return vkdev_ext94;
    case 95:
        return vkdev_ext95;
    case 96:
        return vkdev_ext96;
    case 97:
        return vkdev_ext97;
    case 98:
        return vkdev_ext98;
    case 99:
        return vkdev_ext99;
    case 100:
        return vkdev_ext100;
    case 101:
        return vkdev_ext101;
    case 102:
        return vkdev_ext102;
    case 103:
        return vkdev_ext103;
    case 104:
        return vkdev_ext104;
    case 105:
        return vkdev_ext105;
    case 106:
        return vkdev_ext106;
    case 107:
        return vkdev_ext107;
    case 108:
        return vkdev_ext108;
    case 109:
        return vkdev_ext109;
    case 110:
        return vkdev_ext110;
    case 111:
        return vkdev_ext111;
    case 112:
        return vkdev_ext112;
    case 113:
        return vkdev_ext113;
    case 114:
        return vkdev_ext114;
    case 115:
        return vkdev_ext115;
    case 116:
        return vkdev_ext116;
    case 117:
        return vkdev_ext117;
    case 118:
        return vkdev_ext118;
    case 119:
        return vkdev_ext119;
    case 120:
        return vkdev_ext120;
    case 121:
        return vkdev_ext121;
    case 122:
        return vkdev_ext122;
    case 123:
        return vkdev_ext123;
    case 124:
        return vkdev_ext124;
    case 125:
        return vkdev_ext125;
    case 126:
        return vkdev_ext126;
    case 127:
        return vkdev_ext127;
    case 128:
        return vkdev_ext128;
    case 129:
        return vkdev_ext129;
    case 130:
        return vkdev_ext130;
    case 131:
        return vkdev_ext131;
    case 132:
        return vkdev_ext132;
    case 133:
        return vkdev_ext133;
    case 134:
        return vkdev_ext134;
    case 135:
        return vkdev_ext135;
    case 136:
        return vkdev_ext136;
    case 137:
        return vkdev_ext137;
    case 138:
        return vkdev_ext138;
    case 139:
        return vkdev_ext139;
    case 140:
        return vkdev_ext140;
    case 141:
        return vkdev_ext141;
    case 142:
        return vkdev_ext142;
    case 143:
        return vkdev_ext143;
    case 144:
        return vkdev_ext144;
    case 145:
        return vkdev_ext145;
    case 146:
        return vkdev_ext146;
    case 147:
        return vkdev_ext147;
    case 148:
        return vkdev_ext148;
    case 149:
        return vkdev_ext149;
    case 150:
        return vkdev_ext150;
    case 151:
        return vkdev_ext151;
    case 152:
        return vkdev_ext152;
    case 153:
        return vkdev_ext153;
    case 154:
        return vkdev_ext154;
    case 155:
        return vkdev_ext155;
    case 156:
        return vkdev_ext156;
    case 157:
        return vkdev_ext157;
    case 158:
        return vkdev_ext158;
    case 159:
        return vkdev_ext159;
    case 160:
        return vkdev_ext160;
    case 161:
        return vkdev_ext161;
    case 162:
        return vkdev_ext162;
    case 163:
        return vkdev_ext163;
    case 164:
        return vkdev_ext164;
    case 165:
        return vkdev_ext165;
    case 166:
        return vkdev_ext166;
    case 167:
        return vkdev_ext167;
    case 168:
        return vkdev_ext168;
    case 169:
        return vkdev_ext169;
    case 170:
        return vkdev_ext170;
    case 171:
        return vkdev_ext171;
    case 172:
        return vkdev_ext172;
    case 173:
        return vkdev_ext173;
    case 174:
        return vkdev_ext174;
    case 175:
        return vkdev_ext175;
    case 176:
        return vkdev_ext176;
    case 177:
        return vkdev_ext177;
    case 178:
        return vkdev_ext178;
    case 179:
        return vkdev_ext179;
    case 180:
        return vkdev_ext180;
    case 181:
        return vkdev_ext181;
    case 182:
        return vkdev_ext182;
    case 183:
        return vkdev_ext183;
    case 184:
        return vkdev_ext184;
    case 185:
        return vkdev_ext185;
    case 186:
        return vkdev_ext186;
    case 187:
        return vkdev_ext187;
    case 188:
        return vkdev_ext188;
    case 189:
        return vkdev_ext189;
    case 190:
        return vkdev_ext190;
    case 191:
        return vkdev_ext191;
    case 192:
        return vkdev_ext192;
    case 193:
        return vkdev_ext193;
    case 194:
        return vkdev_ext194;
    case 195:
        return vkdev_ext195;
    case 196:
        return vkdev_ext196;
    case 197:
        return vkdev_ext197;
    case 198:
        return vkdev_ext198;
    case 199:
        return vkdev_ext199;
    case 200:
        return vkdev_ext200;
    case 201:
        return vkdev_ext201;
    case 202:
        return vkdev_ext202;
    case 203:
        return vkdev_ext203;
    case 204:
        return vkdev_ext204;
    case 205:
        return vkdev_ext205;
    case 206:
        return vkdev_ext206;
    case 207:
        return vkdev_ext207;
    case 208:
        return vkdev_ext208;
    case 209:
        return vkdev_ext209;
    case 210:
        return vkdev_ext210;
    case 211:
        return vkdev_ext211;
    case 212:
        return vkdev_ext212;
    case 213:
        return vkdev_ext213;
    case 214:
        return vkdev_ext214;
    case 215:
        return vkdev_ext215;
    case 216:
        return vkdev_ext216;
    case 217:
        return vkdev_ext217;
    case 218:
        return vkdev_ext218;
    case 219:
        return vkdev_ext219;
    case 220:
        return vkdev_ext220;
    case 221:
        return vkdev_ext221;
    case 222:
        return vkdev_ext222;
    case 223:
        return vkdev_ext223;
    case 224:
        return vkdev_ext224;
    case 225:
        return vkdev_ext225;
    case 226:
        return vkdev_ext226;
    case 227:
        return vkdev_ext227;
    case 228:
        return vkdev_ext228;
    case 229:
        return vkdev_ext229;
    case 230:
        return vkdev_ext230;
    case 231:
        return vkdev_ext231;
    case 232:
        return vkdev_ext232;
    case 233:
        return vkdev_ext233;
    case 234:
        return vkdev_ext234;
    case 235:
        return vkdev_ext235;
    case 236:
        return vkdev_ext236;
    case 237:
        return vkdev_ext237;
    case 238:
        return vkdev_ext238;
    case 239:
        return vkdev_ext239;
    case 240:
        return vkdev_ext240;
    case 241:
        return vkdev_ext241;
    case 242:
        return vkdev_ext242;
    case 243:
        return vkdev_ext243;
    case 244:
        return vkdev_ext244;
    case 245:
        return vkdev_ext245;
    case 246:
        return vkdev_ext246;
    case 247:
        return vkdev_ext247;
    case 248:
        return vkdev_ext248;
    case 249:
        return vkdev_ext249;
    }
    return NULL;
}
