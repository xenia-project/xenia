/*
 *
 * Copyright (c) 2016 The Khronos Group Inc.
 * Copyright (c) 2016 Valve Corporation
 * Copyright (c) 2016 LunarG, Inc.
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
 * Author: Mark Young <marky@lunarg.com>
 *
 */

// This code is used to enable generic instance extensions which use a physical device
// as the first parameter.  If the extension is already known by the loader, it will
// not use this code, but instead use the more direct route.  However, if it is
// unknown to the loader, it will use this code.  Technically, this is not trampoline
// code since we don't want to optimize it out.

#include "vk_loader_platform.h"
#include "loader.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize(3)  // force gcc to use tail-calls
#endif

// Trampoline function macro for unknown physical device extension command.
#define PhysDevExtTramp(num)                                                              \
    VKAPI_ATTR void VKAPI_CALL vkPhysDevExtTramp##num(VkPhysicalDevice physical_device) { \
        const struct loader_instance_dispatch_table *disp;                                \
        disp = loader_get_instance_dispatch(physical_device);                             \
        disp->phys_dev_ext[num](loader_unwrap_physical_device(physical_device));          \
    }

// Terminator function macro for unknown physical device extension command.
#define PhysDevExtTermin(num)                                                                                         \
    VKAPI_ATTR void VKAPI_CALL vkPhysDevExtTermin##num(VkPhysicalDevice physical_device) {                            \
        struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physical_device;    \
        struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;                                              \
        struct loader_instance *inst = (struct loader_instance *)icd_term->this_instance;                             \
        if (NULL == icd_term->phys_dev_ext[num]) {                                                                    \
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, "Extension %s not supported for this physical device", \
                       inst->phys_dev_ext_disp_hash[num].func_name);                                                  \
        }                                                                                                             \
        icd_term->phys_dev_ext[num](phys_dev_term->phys_dev);                                                         \
    }

// Disable clang-format for lists of macros
// clang-format off

// Instantiations of the trampoline and terminator
PhysDevExtTramp(0)   PhysDevExtTermin(0)
PhysDevExtTramp(1)   PhysDevExtTermin(1)
PhysDevExtTramp(2)   PhysDevExtTermin(2)
PhysDevExtTramp(3)   PhysDevExtTermin(3)
PhysDevExtTramp(4)   PhysDevExtTermin(4)
PhysDevExtTramp(5)   PhysDevExtTermin(5)
PhysDevExtTramp(6)   PhysDevExtTermin(6)
PhysDevExtTramp(7)   PhysDevExtTermin(7)
PhysDevExtTramp(8)   PhysDevExtTermin(8)
PhysDevExtTramp(9)   PhysDevExtTermin(9)
PhysDevExtTramp(10)  PhysDevExtTermin(10)
PhysDevExtTramp(11)  PhysDevExtTermin(11)
PhysDevExtTramp(12)  PhysDevExtTermin(12)
PhysDevExtTramp(13)  PhysDevExtTermin(13)
PhysDevExtTramp(14)  PhysDevExtTermin(14)
PhysDevExtTramp(15)  PhysDevExtTermin(15)
PhysDevExtTramp(16)  PhysDevExtTermin(16)
PhysDevExtTramp(17)  PhysDevExtTermin(17)
PhysDevExtTramp(18)  PhysDevExtTermin(18)
PhysDevExtTramp(19)  PhysDevExtTermin(19)
PhysDevExtTramp(20)  PhysDevExtTermin(20)
PhysDevExtTramp(21)  PhysDevExtTermin(21)
PhysDevExtTramp(22)  PhysDevExtTermin(22)
PhysDevExtTramp(23)  PhysDevExtTermin(23)
PhysDevExtTramp(24)  PhysDevExtTermin(24)
PhysDevExtTramp(25)  PhysDevExtTermin(25)
PhysDevExtTramp(26)  PhysDevExtTermin(26)
PhysDevExtTramp(27)  PhysDevExtTermin(27)
PhysDevExtTramp(28)  PhysDevExtTermin(28)
PhysDevExtTramp(29)  PhysDevExtTermin(29)
PhysDevExtTramp(30)  PhysDevExtTermin(30)
PhysDevExtTramp(31)  PhysDevExtTermin(31)
PhysDevExtTramp(32)  PhysDevExtTermin(32)
PhysDevExtTramp(33)  PhysDevExtTermin(33)
PhysDevExtTramp(34)  PhysDevExtTermin(34)
PhysDevExtTramp(35)  PhysDevExtTermin(35)
PhysDevExtTramp(36)  PhysDevExtTermin(36)
PhysDevExtTramp(37)  PhysDevExtTermin(37)
PhysDevExtTramp(38)  PhysDevExtTermin(38)
PhysDevExtTramp(39)  PhysDevExtTermin(39)
PhysDevExtTramp(40)  PhysDevExtTermin(40)
PhysDevExtTramp(41)  PhysDevExtTermin(41)
PhysDevExtTramp(42)  PhysDevExtTermin(42)
PhysDevExtTramp(43)  PhysDevExtTermin(43)
PhysDevExtTramp(44)  PhysDevExtTermin(44)
PhysDevExtTramp(45)  PhysDevExtTermin(45)
PhysDevExtTramp(46)  PhysDevExtTermin(46)
PhysDevExtTramp(47)  PhysDevExtTermin(47)
PhysDevExtTramp(48)  PhysDevExtTermin(48)
PhysDevExtTramp(49)  PhysDevExtTermin(49)
PhysDevExtTramp(50)  PhysDevExtTermin(50)
PhysDevExtTramp(51)  PhysDevExtTermin(51)
PhysDevExtTramp(52)  PhysDevExtTermin(52)
PhysDevExtTramp(53)  PhysDevExtTermin(53)
PhysDevExtTramp(54)  PhysDevExtTermin(54)
PhysDevExtTramp(55)  PhysDevExtTermin(55)
PhysDevExtTramp(56)  PhysDevExtTermin(56)
PhysDevExtTramp(57)  PhysDevExtTermin(57)
PhysDevExtTramp(58)  PhysDevExtTermin(58)
PhysDevExtTramp(59)  PhysDevExtTermin(59)
PhysDevExtTramp(60)  PhysDevExtTermin(60)
PhysDevExtTramp(61)  PhysDevExtTermin(61)
PhysDevExtTramp(62)  PhysDevExtTermin(62)
PhysDevExtTramp(63)  PhysDevExtTermin(63)
PhysDevExtTramp(64)  PhysDevExtTermin(64)
PhysDevExtTramp(65)  PhysDevExtTermin(65)
PhysDevExtTramp(66)  PhysDevExtTermin(66)
PhysDevExtTramp(67)  PhysDevExtTermin(67)
PhysDevExtTramp(68)  PhysDevExtTermin(68)
PhysDevExtTramp(69)  PhysDevExtTermin(69)
PhysDevExtTramp(70)  PhysDevExtTermin(70)
PhysDevExtTramp(71)  PhysDevExtTermin(71)
PhysDevExtTramp(72)  PhysDevExtTermin(72)
PhysDevExtTramp(73)  PhysDevExtTermin(73)
PhysDevExtTramp(74)  PhysDevExtTermin(74)
PhysDevExtTramp(75)  PhysDevExtTermin(75)
PhysDevExtTramp(76)  PhysDevExtTermin(76)
PhysDevExtTramp(77)  PhysDevExtTermin(77)
PhysDevExtTramp(78)  PhysDevExtTermin(78)
PhysDevExtTramp(79)  PhysDevExtTermin(79)
PhysDevExtTramp(80)  PhysDevExtTermin(80)
PhysDevExtTramp(81)  PhysDevExtTermin(81)
PhysDevExtTramp(82)  PhysDevExtTermin(82)
PhysDevExtTramp(83)  PhysDevExtTermin(83)
PhysDevExtTramp(84)  PhysDevExtTermin(84)
PhysDevExtTramp(85)  PhysDevExtTermin(85)
PhysDevExtTramp(86)  PhysDevExtTermin(86)
PhysDevExtTramp(87)  PhysDevExtTermin(87)
PhysDevExtTramp(88)  PhysDevExtTermin(88)
PhysDevExtTramp(89)  PhysDevExtTermin(89)
PhysDevExtTramp(90)  PhysDevExtTermin(90)
PhysDevExtTramp(91)  PhysDevExtTermin(91)
PhysDevExtTramp(92)  PhysDevExtTermin(92)
PhysDevExtTramp(93)  PhysDevExtTermin(93)
PhysDevExtTramp(94)  PhysDevExtTermin(94)
PhysDevExtTramp(95)  PhysDevExtTermin(95)
PhysDevExtTramp(96)  PhysDevExtTermin(96)
PhysDevExtTramp(97)  PhysDevExtTermin(97)
PhysDevExtTramp(98)  PhysDevExtTermin(98)
PhysDevExtTramp(99)  PhysDevExtTermin(99)
PhysDevExtTramp(100) PhysDevExtTermin(100)
PhysDevExtTramp(101) PhysDevExtTermin(101)
PhysDevExtTramp(102) PhysDevExtTermin(102)
PhysDevExtTramp(103) PhysDevExtTermin(103)
PhysDevExtTramp(104) PhysDevExtTermin(104)
PhysDevExtTramp(105) PhysDevExtTermin(105)
PhysDevExtTramp(106) PhysDevExtTermin(106)
PhysDevExtTramp(107) PhysDevExtTermin(107)
PhysDevExtTramp(108) PhysDevExtTermin(108)
PhysDevExtTramp(109) PhysDevExtTermin(109)
PhysDevExtTramp(110) PhysDevExtTermin(110)
PhysDevExtTramp(111) PhysDevExtTermin(111)
PhysDevExtTramp(112) PhysDevExtTermin(112)
PhysDevExtTramp(113) PhysDevExtTermin(113)
PhysDevExtTramp(114) PhysDevExtTermin(114)
PhysDevExtTramp(115) PhysDevExtTermin(115)
PhysDevExtTramp(116) PhysDevExtTermin(116)
PhysDevExtTramp(117) PhysDevExtTermin(117)
PhysDevExtTramp(118) PhysDevExtTermin(118)
PhysDevExtTramp(119) PhysDevExtTermin(119)
PhysDevExtTramp(120) PhysDevExtTermin(120)
PhysDevExtTramp(121) PhysDevExtTermin(121)
PhysDevExtTramp(122) PhysDevExtTermin(122)
PhysDevExtTramp(123) PhysDevExtTermin(123)
PhysDevExtTramp(124) PhysDevExtTermin(124)
PhysDevExtTramp(125) PhysDevExtTermin(125)
PhysDevExtTramp(126) PhysDevExtTermin(126)
PhysDevExtTramp(127) PhysDevExtTermin(127)
PhysDevExtTramp(128) PhysDevExtTermin(128)
PhysDevExtTramp(129) PhysDevExtTermin(129)
PhysDevExtTramp(130) PhysDevExtTermin(130)
PhysDevExtTramp(131) PhysDevExtTermin(131)
PhysDevExtTramp(132) PhysDevExtTermin(132)
PhysDevExtTramp(133) PhysDevExtTermin(133)
PhysDevExtTramp(134) PhysDevExtTermin(134)
PhysDevExtTramp(135) PhysDevExtTermin(135)
PhysDevExtTramp(136) PhysDevExtTermin(136)
PhysDevExtTramp(137) PhysDevExtTermin(137)
PhysDevExtTramp(138) PhysDevExtTermin(138)
PhysDevExtTramp(139) PhysDevExtTermin(139)
PhysDevExtTramp(140) PhysDevExtTermin(140)
PhysDevExtTramp(141) PhysDevExtTermin(141)
PhysDevExtTramp(142) PhysDevExtTermin(142)
PhysDevExtTramp(143) PhysDevExtTermin(143)
PhysDevExtTramp(144) PhysDevExtTermin(144)
PhysDevExtTramp(145) PhysDevExtTermin(145)
PhysDevExtTramp(146) PhysDevExtTermin(146)
PhysDevExtTramp(147) PhysDevExtTermin(147)
PhysDevExtTramp(148) PhysDevExtTermin(148)
PhysDevExtTramp(149) PhysDevExtTermin(149)
PhysDevExtTramp(150) PhysDevExtTermin(150)
PhysDevExtTramp(151) PhysDevExtTermin(151)
PhysDevExtTramp(152) PhysDevExtTermin(152)
PhysDevExtTramp(153) PhysDevExtTermin(153)
PhysDevExtTramp(154) PhysDevExtTermin(154)
PhysDevExtTramp(155) PhysDevExtTermin(155)
PhysDevExtTramp(156) PhysDevExtTermin(156)
PhysDevExtTramp(157) PhysDevExtTermin(157)
PhysDevExtTramp(158) PhysDevExtTermin(158)
PhysDevExtTramp(159) PhysDevExtTermin(159)
PhysDevExtTramp(160) PhysDevExtTermin(160)
PhysDevExtTramp(161) PhysDevExtTermin(161)
PhysDevExtTramp(162) PhysDevExtTermin(162)
PhysDevExtTramp(163) PhysDevExtTermin(163)
PhysDevExtTramp(164) PhysDevExtTermin(164)
PhysDevExtTramp(165) PhysDevExtTermin(165)
PhysDevExtTramp(166) PhysDevExtTermin(166)
PhysDevExtTramp(167) PhysDevExtTermin(167)
PhysDevExtTramp(168) PhysDevExtTermin(168)
PhysDevExtTramp(169) PhysDevExtTermin(169)
PhysDevExtTramp(170) PhysDevExtTermin(170)
PhysDevExtTramp(171) PhysDevExtTermin(171)
PhysDevExtTramp(172) PhysDevExtTermin(172)
PhysDevExtTramp(173) PhysDevExtTermin(173)
PhysDevExtTramp(174) PhysDevExtTermin(174)
PhysDevExtTramp(175) PhysDevExtTermin(175)
PhysDevExtTramp(176) PhysDevExtTermin(176)
PhysDevExtTramp(177) PhysDevExtTermin(177)
PhysDevExtTramp(178) PhysDevExtTermin(178)
PhysDevExtTramp(179) PhysDevExtTermin(179)
PhysDevExtTramp(180) PhysDevExtTermin(180)
PhysDevExtTramp(181) PhysDevExtTermin(181)
PhysDevExtTramp(182) PhysDevExtTermin(182)
PhysDevExtTramp(183) PhysDevExtTermin(183)
PhysDevExtTramp(184) PhysDevExtTermin(184)
PhysDevExtTramp(185) PhysDevExtTermin(185)
PhysDevExtTramp(186) PhysDevExtTermin(186)
PhysDevExtTramp(187) PhysDevExtTermin(187)
PhysDevExtTramp(188) PhysDevExtTermin(188)
PhysDevExtTramp(189) PhysDevExtTermin(189)
PhysDevExtTramp(190) PhysDevExtTermin(190)
PhysDevExtTramp(191) PhysDevExtTermin(191)
PhysDevExtTramp(192) PhysDevExtTermin(192)
PhysDevExtTramp(193) PhysDevExtTermin(193)
PhysDevExtTramp(194) PhysDevExtTermin(194)
PhysDevExtTramp(195) PhysDevExtTermin(195)
PhysDevExtTramp(196) PhysDevExtTermin(196)
PhysDevExtTramp(197) PhysDevExtTermin(197)
PhysDevExtTramp(198) PhysDevExtTermin(198)
PhysDevExtTramp(199) PhysDevExtTermin(199)
PhysDevExtTramp(200) PhysDevExtTermin(200)
PhysDevExtTramp(201) PhysDevExtTermin(201)
PhysDevExtTramp(202) PhysDevExtTermin(202)
PhysDevExtTramp(203) PhysDevExtTermin(203)
PhysDevExtTramp(204) PhysDevExtTermin(204)
PhysDevExtTramp(205) PhysDevExtTermin(205)
PhysDevExtTramp(206) PhysDevExtTermin(206)
PhysDevExtTramp(207) PhysDevExtTermin(207)
PhysDevExtTramp(208) PhysDevExtTermin(208)
PhysDevExtTramp(209) PhysDevExtTermin(209)
PhysDevExtTramp(210) PhysDevExtTermin(210)
PhysDevExtTramp(211) PhysDevExtTermin(211)
PhysDevExtTramp(212) PhysDevExtTermin(212)
PhysDevExtTramp(213) PhysDevExtTermin(213)
PhysDevExtTramp(214) PhysDevExtTermin(214)
PhysDevExtTramp(215) PhysDevExtTermin(215)
PhysDevExtTramp(216) PhysDevExtTermin(216)
PhysDevExtTramp(217) PhysDevExtTermin(217)
PhysDevExtTramp(218) PhysDevExtTermin(218)
PhysDevExtTramp(219) PhysDevExtTermin(219)
PhysDevExtTramp(220) PhysDevExtTermin(220)
PhysDevExtTramp(221) PhysDevExtTermin(221)
PhysDevExtTramp(222) PhysDevExtTermin(222)
PhysDevExtTramp(223) PhysDevExtTermin(223)
PhysDevExtTramp(224) PhysDevExtTermin(224)
PhysDevExtTramp(225) PhysDevExtTermin(225)
PhysDevExtTramp(226) PhysDevExtTermin(226)
PhysDevExtTramp(227) PhysDevExtTermin(227)
PhysDevExtTramp(228) PhysDevExtTermin(228)
PhysDevExtTramp(229) PhysDevExtTermin(229)
PhysDevExtTramp(230) PhysDevExtTermin(230)
PhysDevExtTramp(231) PhysDevExtTermin(231)
PhysDevExtTramp(232) PhysDevExtTermin(232)
PhysDevExtTramp(233) PhysDevExtTermin(233)
PhysDevExtTramp(234) PhysDevExtTermin(234)
PhysDevExtTramp(235) PhysDevExtTermin(235)
PhysDevExtTramp(236) PhysDevExtTermin(236)
PhysDevExtTramp(237) PhysDevExtTermin(237)
PhysDevExtTramp(238) PhysDevExtTermin(238)
PhysDevExtTramp(239) PhysDevExtTermin(239)
PhysDevExtTramp(240) PhysDevExtTermin(240)
PhysDevExtTramp(241) PhysDevExtTermin(241)
PhysDevExtTramp(242) PhysDevExtTermin(242)
PhysDevExtTramp(243) PhysDevExtTermin(243)
PhysDevExtTramp(244) PhysDevExtTermin(244)
PhysDevExtTramp(245) PhysDevExtTermin(245)
PhysDevExtTramp(246) PhysDevExtTermin(246)
PhysDevExtTramp(247) PhysDevExtTermin(247)
PhysDevExtTramp(248) PhysDevExtTermin(248)
PhysDevExtTramp(249) PhysDevExtTermin(249)


void *loader_get_phys_dev_ext_tramp(uint32_t index) {
    switch (index) {
#define TRAMP_CASE_HANDLE(num) case num: return vkPhysDevExtTramp##num
        TRAMP_CASE_HANDLE(0);
        TRAMP_CASE_HANDLE(1);
        TRAMP_CASE_HANDLE(2);
        TRAMP_CASE_HANDLE(3);
        TRAMP_CASE_HANDLE(4);
        TRAMP_CASE_HANDLE(5);
        TRAMP_CASE_HANDLE(6);
        TRAMP_CASE_HANDLE(7);
        TRAMP_CASE_HANDLE(8);
        TRAMP_CASE_HANDLE(9);
        TRAMP_CASE_HANDLE(10);
        TRAMP_CASE_HANDLE(11);
        TRAMP_CASE_HANDLE(12);
        TRAMP_CASE_HANDLE(13);
        TRAMP_CASE_HANDLE(14);
        TRAMP_CASE_HANDLE(15);
        TRAMP_CASE_HANDLE(16);
        TRAMP_CASE_HANDLE(17);
        TRAMP_CASE_HANDLE(18);
        TRAMP_CASE_HANDLE(19);
        TRAMP_CASE_HANDLE(20);
        TRAMP_CASE_HANDLE(21);
        TRAMP_CASE_HANDLE(22);
        TRAMP_CASE_HANDLE(23);
        TRAMP_CASE_HANDLE(24);
        TRAMP_CASE_HANDLE(25);
        TRAMP_CASE_HANDLE(26);
        TRAMP_CASE_HANDLE(27);
        TRAMP_CASE_HANDLE(28);
        TRAMP_CASE_HANDLE(29);
        TRAMP_CASE_HANDLE(30);
        TRAMP_CASE_HANDLE(31);
        TRAMP_CASE_HANDLE(32);
        TRAMP_CASE_HANDLE(33);
        TRAMP_CASE_HANDLE(34);
        TRAMP_CASE_HANDLE(35);
        TRAMP_CASE_HANDLE(36);
        TRAMP_CASE_HANDLE(37);
        TRAMP_CASE_HANDLE(38);
        TRAMP_CASE_HANDLE(39);
        TRAMP_CASE_HANDLE(40);
        TRAMP_CASE_HANDLE(41);
        TRAMP_CASE_HANDLE(42);
        TRAMP_CASE_HANDLE(43);
        TRAMP_CASE_HANDLE(44);
        TRAMP_CASE_HANDLE(45);
        TRAMP_CASE_HANDLE(46);
        TRAMP_CASE_HANDLE(47);
        TRAMP_CASE_HANDLE(48);
        TRAMP_CASE_HANDLE(49);
        TRAMP_CASE_HANDLE(50);
        TRAMP_CASE_HANDLE(51);
        TRAMP_CASE_HANDLE(52);
        TRAMP_CASE_HANDLE(53);
        TRAMP_CASE_HANDLE(54);
        TRAMP_CASE_HANDLE(55);
        TRAMP_CASE_HANDLE(56);
        TRAMP_CASE_HANDLE(57);
        TRAMP_CASE_HANDLE(58);
        TRAMP_CASE_HANDLE(59);
        TRAMP_CASE_HANDLE(60);
        TRAMP_CASE_HANDLE(61);
        TRAMP_CASE_HANDLE(62);
        TRAMP_CASE_HANDLE(63);
        TRAMP_CASE_HANDLE(64);
        TRAMP_CASE_HANDLE(65);
        TRAMP_CASE_HANDLE(66);
        TRAMP_CASE_HANDLE(67);
        TRAMP_CASE_HANDLE(68);
        TRAMP_CASE_HANDLE(69);
        TRAMP_CASE_HANDLE(70);
        TRAMP_CASE_HANDLE(71);
        TRAMP_CASE_HANDLE(72);
        TRAMP_CASE_HANDLE(73);
        TRAMP_CASE_HANDLE(74);
        TRAMP_CASE_HANDLE(75);
        TRAMP_CASE_HANDLE(76);
        TRAMP_CASE_HANDLE(77);
        TRAMP_CASE_HANDLE(78);
        TRAMP_CASE_HANDLE(79);
        TRAMP_CASE_HANDLE(80);
        TRAMP_CASE_HANDLE(81);
        TRAMP_CASE_HANDLE(82);
        TRAMP_CASE_HANDLE(83);
        TRAMP_CASE_HANDLE(84);
        TRAMP_CASE_HANDLE(85);
        TRAMP_CASE_HANDLE(86);
        TRAMP_CASE_HANDLE(87);
        TRAMP_CASE_HANDLE(88);
        TRAMP_CASE_HANDLE(89);
        TRAMP_CASE_HANDLE(90);
        TRAMP_CASE_HANDLE(91);
        TRAMP_CASE_HANDLE(92);
        TRAMP_CASE_HANDLE(93);
        TRAMP_CASE_HANDLE(94);
        TRAMP_CASE_HANDLE(95);
        TRAMP_CASE_HANDLE(96);
        TRAMP_CASE_HANDLE(97);
        TRAMP_CASE_HANDLE(98);
        TRAMP_CASE_HANDLE(99);
        TRAMP_CASE_HANDLE(100);
        TRAMP_CASE_HANDLE(101);
        TRAMP_CASE_HANDLE(102);
        TRAMP_CASE_HANDLE(103);
        TRAMP_CASE_HANDLE(104);
        TRAMP_CASE_HANDLE(105);
        TRAMP_CASE_HANDLE(106);
        TRAMP_CASE_HANDLE(107);
        TRAMP_CASE_HANDLE(108);
        TRAMP_CASE_HANDLE(109);
        TRAMP_CASE_HANDLE(110);
        TRAMP_CASE_HANDLE(111);
        TRAMP_CASE_HANDLE(112);
        TRAMP_CASE_HANDLE(113);
        TRAMP_CASE_HANDLE(114);
        TRAMP_CASE_HANDLE(115);
        TRAMP_CASE_HANDLE(116);
        TRAMP_CASE_HANDLE(117);
        TRAMP_CASE_HANDLE(118);
        TRAMP_CASE_HANDLE(119);
        TRAMP_CASE_HANDLE(120);
        TRAMP_CASE_HANDLE(121);
        TRAMP_CASE_HANDLE(122);
        TRAMP_CASE_HANDLE(123);
        TRAMP_CASE_HANDLE(124);
        TRAMP_CASE_HANDLE(125);
        TRAMP_CASE_HANDLE(126);
        TRAMP_CASE_HANDLE(127);
        TRAMP_CASE_HANDLE(128);
        TRAMP_CASE_HANDLE(129);
        TRAMP_CASE_HANDLE(130);
        TRAMP_CASE_HANDLE(131);
        TRAMP_CASE_HANDLE(132);
        TRAMP_CASE_HANDLE(133);
        TRAMP_CASE_HANDLE(134);
        TRAMP_CASE_HANDLE(135);
        TRAMP_CASE_HANDLE(136);
        TRAMP_CASE_HANDLE(137);
        TRAMP_CASE_HANDLE(138);
        TRAMP_CASE_HANDLE(139);
        TRAMP_CASE_HANDLE(140);
        TRAMP_CASE_HANDLE(141);
        TRAMP_CASE_HANDLE(142);
        TRAMP_CASE_HANDLE(143);
        TRAMP_CASE_HANDLE(144);
        TRAMP_CASE_HANDLE(145);
        TRAMP_CASE_HANDLE(146);
        TRAMP_CASE_HANDLE(147);
        TRAMP_CASE_HANDLE(148);
        TRAMP_CASE_HANDLE(149);
        TRAMP_CASE_HANDLE(150);
        TRAMP_CASE_HANDLE(151);
        TRAMP_CASE_HANDLE(152);
        TRAMP_CASE_HANDLE(153);
        TRAMP_CASE_HANDLE(154);
        TRAMP_CASE_HANDLE(155);
        TRAMP_CASE_HANDLE(156);
        TRAMP_CASE_HANDLE(157);
        TRAMP_CASE_HANDLE(158);
        TRAMP_CASE_HANDLE(159);
        TRAMP_CASE_HANDLE(160);
        TRAMP_CASE_HANDLE(161);
        TRAMP_CASE_HANDLE(162);
        TRAMP_CASE_HANDLE(163);
        TRAMP_CASE_HANDLE(164);
        TRAMP_CASE_HANDLE(165);
        TRAMP_CASE_HANDLE(166);
        TRAMP_CASE_HANDLE(167);
        TRAMP_CASE_HANDLE(168);
        TRAMP_CASE_HANDLE(169);
        TRAMP_CASE_HANDLE(170);
        TRAMP_CASE_HANDLE(171);
        TRAMP_CASE_HANDLE(172);
        TRAMP_CASE_HANDLE(173);
        TRAMP_CASE_HANDLE(174);
        TRAMP_CASE_HANDLE(175);
        TRAMP_CASE_HANDLE(176);
        TRAMP_CASE_HANDLE(177);
        TRAMP_CASE_HANDLE(178);
        TRAMP_CASE_HANDLE(179);
        TRAMP_CASE_HANDLE(180);
        TRAMP_CASE_HANDLE(181);
        TRAMP_CASE_HANDLE(182);
        TRAMP_CASE_HANDLE(183);
        TRAMP_CASE_HANDLE(184);
        TRAMP_CASE_HANDLE(185);
        TRAMP_CASE_HANDLE(186);
        TRAMP_CASE_HANDLE(187);
        TRAMP_CASE_HANDLE(188);
        TRAMP_CASE_HANDLE(189);
        TRAMP_CASE_HANDLE(190);
        TRAMP_CASE_HANDLE(191);
        TRAMP_CASE_HANDLE(192);
        TRAMP_CASE_HANDLE(193);
        TRAMP_CASE_HANDLE(194);
        TRAMP_CASE_HANDLE(195);
        TRAMP_CASE_HANDLE(196);
        TRAMP_CASE_HANDLE(197);
        TRAMP_CASE_HANDLE(198);
        TRAMP_CASE_HANDLE(199);
        TRAMP_CASE_HANDLE(200);
        TRAMP_CASE_HANDLE(201);
        TRAMP_CASE_HANDLE(202);
        TRAMP_CASE_HANDLE(203);
        TRAMP_CASE_HANDLE(204);
        TRAMP_CASE_HANDLE(205);
        TRAMP_CASE_HANDLE(206);
        TRAMP_CASE_HANDLE(207);
        TRAMP_CASE_HANDLE(208);
        TRAMP_CASE_HANDLE(209);
        TRAMP_CASE_HANDLE(210);
        TRAMP_CASE_HANDLE(211);
        TRAMP_CASE_HANDLE(212);
        TRAMP_CASE_HANDLE(213);
        TRAMP_CASE_HANDLE(214);
        TRAMP_CASE_HANDLE(215);
        TRAMP_CASE_HANDLE(216);
        TRAMP_CASE_HANDLE(217);
        TRAMP_CASE_HANDLE(218);
        TRAMP_CASE_HANDLE(219);
        TRAMP_CASE_HANDLE(220);
        TRAMP_CASE_HANDLE(221);
        TRAMP_CASE_HANDLE(222);
        TRAMP_CASE_HANDLE(223);
        TRAMP_CASE_HANDLE(224);
        TRAMP_CASE_HANDLE(225);
        TRAMP_CASE_HANDLE(226);
        TRAMP_CASE_HANDLE(227);
        TRAMP_CASE_HANDLE(228);
        TRAMP_CASE_HANDLE(229);
        TRAMP_CASE_HANDLE(230);
        TRAMP_CASE_HANDLE(231);
        TRAMP_CASE_HANDLE(232);
        TRAMP_CASE_HANDLE(233);
        TRAMP_CASE_HANDLE(234);
        TRAMP_CASE_HANDLE(235);
        TRAMP_CASE_HANDLE(236);
        TRAMP_CASE_HANDLE(237);
        TRAMP_CASE_HANDLE(238);
        TRAMP_CASE_HANDLE(239);
        TRAMP_CASE_HANDLE(240);
        TRAMP_CASE_HANDLE(241);
        TRAMP_CASE_HANDLE(242);
        TRAMP_CASE_HANDLE(243);
        TRAMP_CASE_HANDLE(244);
        TRAMP_CASE_HANDLE(245);
        TRAMP_CASE_HANDLE(246);
        TRAMP_CASE_HANDLE(247);
        TRAMP_CASE_HANDLE(248);
        TRAMP_CASE_HANDLE(249);
    }
    return NULL;
}

void *loader_get_phys_dev_ext_termin(uint32_t index) {
    switch (index) {
#define TERM_CASE_HANDLE(num) case num: return vkPhysDevExtTermin##num
        TERM_CASE_HANDLE(0);
        TERM_CASE_HANDLE(1);
        TERM_CASE_HANDLE(2);
        TERM_CASE_HANDLE(3);
        TERM_CASE_HANDLE(4);
        TERM_CASE_HANDLE(5);
        TERM_CASE_HANDLE(6);
        TERM_CASE_HANDLE(7);
        TERM_CASE_HANDLE(8);
        TERM_CASE_HANDLE(9);
        TERM_CASE_HANDLE(10);
        TERM_CASE_HANDLE(11);
        TERM_CASE_HANDLE(12);
        TERM_CASE_HANDLE(13);
        TERM_CASE_HANDLE(14);
        TERM_CASE_HANDLE(15);
        TERM_CASE_HANDLE(16);
        TERM_CASE_HANDLE(17);
        TERM_CASE_HANDLE(18);
        TERM_CASE_HANDLE(19);
        TERM_CASE_HANDLE(20);
        TERM_CASE_HANDLE(21);
        TERM_CASE_HANDLE(22);
        TERM_CASE_HANDLE(23);
        TERM_CASE_HANDLE(24);
        TERM_CASE_HANDLE(25);
        TERM_CASE_HANDLE(26);
        TERM_CASE_HANDLE(27);
        TERM_CASE_HANDLE(28);
        TERM_CASE_HANDLE(29);
        TERM_CASE_HANDLE(30);
        TERM_CASE_HANDLE(31);
        TERM_CASE_HANDLE(32);
        TERM_CASE_HANDLE(33);
        TERM_CASE_HANDLE(34);
        TERM_CASE_HANDLE(35);
        TERM_CASE_HANDLE(36);
        TERM_CASE_HANDLE(37);
        TERM_CASE_HANDLE(38);
        TERM_CASE_HANDLE(39);
        TERM_CASE_HANDLE(40);
        TERM_CASE_HANDLE(41);
        TERM_CASE_HANDLE(42);
        TERM_CASE_HANDLE(43);
        TERM_CASE_HANDLE(44);
        TERM_CASE_HANDLE(45);
        TERM_CASE_HANDLE(46);
        TERM_CASE_HANDLE(47);
        TERM_CASE_HANDLE(48);
        TERM_CASE_HANDLE(49);
        TERM_CASE_HANDLE(50);
        TERM_CASE_HANDLE(51);
        TERM_CASE_HANDLE(52);
        TERM_CASE_HANDLE(53);
        TERM_CASE_HANDLE(54);
        TERM_CASE_HANDLE(55);
        TERM_CASE_HANDLE(56);
        TERM_CASE_HANDLE(57);
        TERM_CASE_HANDLE(58);
        TERM_CASE_HANDLE(59);
        TERM_CASE_HANDLE(60);
        TERM_CASE_HANDLE(61);
        TERM_CASE_HANDLE(62);
        TERM_CASE_HANDLE(63);
        TERM_CASE_HANDLE(64);
        TERM_CASE_HANDLE(65);
        TERM_CASE_HANDLE(66);
        TERM_CASE_HANDLE(67);
        TERM_CASE_HANDLE(68);
        TERM_CASE_HANDLE(69);
        TERM_CASE_HANDLE(70);
        TERM_CASE_HANDLE(71);
        TERM_CASE_HANDLE(72);
        TERM_CASE_HANDLE(73);
        TERM_CASE_HANDLE(74);
        TERM_CASE_HANDLE(75);
        TERM_CASE_HANDLE(76);
        TERM_CASE_HANDLE(77);
        TERM_CASE_HANDLE(78);
        TERM_CASE_HANDLE(79);
        TERM_CASE_HANDLE(80);
        TERM_CASE_HANDLE(81);
        TERM_CASE_HANDLE(82);
        TERM_CASE_HANDLE(83);
        TERM_CASE_HANDLE(84);
        TERM_CASE_HANDLE(85);
        TERM_CASE_HANDLE(86);
        TERM_CASE_HANDLE(87);
        TERM_CASE_HANDLE(88);
        TERM_CASE_HANDLE(89);
        TERM_CASE_HANDLE(90);
        TERM_CASE_HANDLE(91);
        TERM_CASE_HANDLE(92);
        TERM_CASE_HANDLE(93);
        TERM_CASE_HANDLE(94);
        TERM_CASE_HANDLE(95);
        TERM_CASE_HANDLE(96);
        TERM_CASE_HANDLE(97);
        TERM_CASE_HANDLE(98);
        TERM_CASE_HANDLE(99);
        TERM_CASE_HANDLE(100);
        TERM_CASE_HANDLE(101);
        TERM_CASE_HANDLE(102);
        TERM_CASE_HANDLE(103);
        TERM_CASE_HANDLE(104);
        TERM_CASE_HANDLE(105);
        TERM_CASE_HANDLE(106);
        TERM_CASE_HANDLE(107);
        TERM_CASE_HANDLE(108);
        TERM_CASE_HANDLE(109);
        TERM_CASE_HANDLE(110);
        TERM_CASE_HANDLE(111);
        TERM_CASE_HANDLE(112);
        TERM_CASE_HANDLE(113);
        TERM_CASE_HANDLE(114);
        TERM_CASE_HANDLE(115);
        TERM_CASE_HANDLE(116);
        TERM_CASE_HANDLE(117);
        TERM_CASE_HANDLE(118);
        TERM_CASE_HANDLE(119);
        TERM_CASE_HANDLE(120);
        TERM_CASE_HANDLE(121);
        TERM_CASE_HANDLE(122);
        TERM_CASE_HANDLE(123);
        TERM_CASE_HANDLE(124);
        TERM_CASE_HANDLE(125);
        TERM_CASE_HANDLE(126);
        TERM_CASE_HANDLE(127);
        TERM_CASE_HANDLE(128);
        TERM_CASE_HANDLE(129);
        TERM_CASE_HANDLE(130);
        TERM_CASE_HANDLE(131);
        TERM_CASE_HANDLE(132);
        TERM_CASE_HANDLE(133);
        TERM_CASE_HANDLE(134);
        TERM_CASE_HANDLE(135);
        TERM_CASE_HANDLE(136);
        TERM_CASE_HANDLE(137);
        TERM_CASE_HANDLE(138);
        TERM_CASE_HANDLE(139);
        TERM_CASE_HANDLE(140);
        TERM_CASE_HANDLE(141);
        TERM_CASE_HANDLE(142);
        TERM_CASE_HANDLE(143);
        TERM_CASE_HANDLE(144);
        TERM_CASE_HANDLE(145);
        TERM_CASE_HANDLE(146);
        TERM_CASE_HANDLE(147);
        TERM_CASE_HANDLE(148);
        TERM_CASE_HANDLE(149);
        TERM_CASE_HANDLE(150);
        TERM_CASE_HANDLE(151);
        TERM_CASE_HANDLE(152);
        TERM_CASE_HANDLE(153);
        TERM_CASE_HANDLE(154);
        TERM_CASE_HANDLE(155);
        TERM_CASE_HANDLE(156);
        TERM_CASE_HANDLE(157);
        TERM_CASE_HANDLE(158);
        TERM_CASE_HANDLE(159);
        TERM_CASE_HANDLE(160);
        TERM_CASE_HANDLE(161);
        TERM_CASE_HANDLE(162);
        TERM_CASE_HANDLE(163);
        TERM_CASE_HANDLE(164);
        TERM_CASE_HANDLE(165);
        TERM_CASE_HANDLE(166);
        TERM_CASE_HANDLE(167);
        TERM_CASE_HANDLE(168);
        TERM_CASE_HANDLE(169);
        TERM_CASE_HANDLE(170);
        TERM_CASE_HANDLE(171);
        TERM_CASE_HANDLE(172);
        TERM_CASE_HANDLE(173);
        TERM_CASE_HANDLE(174);
        TERM_CASE_HANDLE(175);
        TERM_CASE_HANDLE(176);
        TERM_CASE_HANDLE(177);
        TERM_CASE_HANDLE(178);
        TERM_CASE_HANDLE(179);
        TERM_CASE_HANDLE(180);
        TERM_CASE_HANDLE(181);
        TERM_CASE_HANDLE(182);
        TERM_CASE_HANDLE(183);
        TERM_CASE_HANDLE(184);
        TERM_CASE_HANDLE(185);
        TERM_CASE_HANDLE(186);
        TERM_CASE_HANDLE(187);
        TERM_CASE_HANDLE(188);
        TERM_CASE_HANDLE(189);
        TERM_CASE_HANDLE(190);
        TERM_CASE_HANDLE(191);
        TERM_CASE_HANDLE(192);
        TERM_CASE_HANDLE(193);
        TERM_CASE_HANDLE(194);
        TERM_CASE_HANDLE(195);
        TERM_CASE_HANDLE(196);
        TERM_CASE_HANDLE(197);
        TERM_CASE_HANDLE(198);
        TERM_CASE_HANDLE(199);
        TERM_CASE_HANDLE(200);
        TERM_CASE_HANDLE(201);
        TERM_CASE_HANDLE(202);
        TERM_CASE_HANDLE(203);
        TERM_CASE_HANDLE(204);
        TERM_CASE_HANDLE(205);
        TERM_CASE_HANDLE(206);
        TERM_CASE_HANDLE(207);
        TERM_CASE_HANDLE(208);
        TERM_CASE_HANDLE(209);
        TERM_CASE_HANDLE(210);
        TERM_CASE_HANDLE(211);
        TERM_CASE_HANDLE(212);
        TERM_CASE_HANDLE(213);
        TERM_CASE_HANDLE(214);
        TERM_CASE_HANDLE(215);
        TERM_CASE_HANDLE(216);
        TERM_CASE_HANDLE(217);
        TERM_CASE_HANDLE(218);
        TERM_CASE_HANDLE(219);
        TERM_CASE_HANDLE(220);
        TERM_CASE_HANDLE(221);
        TERM_CASE_HANDLE(222);
        TERM_CASE_HANDLE(223);
        TERM_CASE_HANDLE(224);
        TERM_CASE_HANDLE(225);
        TERM_CASE_HANDLE(226);
        TERM_CASE_HANDLE(227);
        TERM_CASE_HANDLE(228);
        TERM_CASE_HANDLE(229);
        TERM_CASE_HANDLE(230);
        TERM_CASE_HANDLE(231);
        TERM_CASE_HANDLE(232);
        TERM_CASE_HANDLE(233);
        TERM_CASE_HANDLE(234);
        TERM_CASE_HANDLE(235);
        TERM_CASE_HANDLE(236);
        TERM_CASE_HANDLE(237);
        TERM_CASE_HANDLE(238);
        TERM_CASE_HANDLE(239);
        TERM_CASE_HANDLE(240);
        TERM_CASE_HANDLE(241);
        TERM_CASE_HANDLE(242);
        TERM_CASE_HANDLE(243);
        TERM_CASE_HANDLE(244);
        TERM_CASE_HANDLE(245);
        TERM_CASE_HANDLE(246);
        TERM_CASE_HANDLE(247);
        TERM_CASE_HANDLE(248);
        TERM_CASE_HANDLE(249);
    }
    return NULL;
}
