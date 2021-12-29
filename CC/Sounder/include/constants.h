
class Consts {
 public:
  static constexpr size_t kFftSize_80211 = 64;
  static constexpr size_t kNumMappedSubcarriers_80211 = 52;
  // Define freq-domain STS according to
  // https://standards.ieee.org/standard/802_11a-1999.html
  static constexpr std::complex<float> sts_seq[64] = {
      {0, 0},   {0, 0}, {0, 0}, {0, 0}, {0, 0},   {0, 0}, {0, 0}, {0, 0},
      {1, 1},   {0, 0}, {0, 0}, {0, 0}, {-1, -1}, {0, 0}, {0, 0}, {0, 0},
      {1, 1},   {0, 0}, {0, 0}, {0, 0}, {-1, -1}, {0, 0}, {0, 0}, {0, 0},
      {-1, -1}, {0, 0}, {0, 0}, {0, 0}, {1, 1},   {0, 0}, {0, 0}, {0, 0},
      {0, 0},   {0, 0}, {0, 0}, {0, 0}, {-1, -1}, {0, 0}, {0, 0}, {0, 0},
      {-1, -1}, {0, 0}, {0, 0}, {0, 0}, {1, 1},   {0, 0}, {0, 0}, {0, 0},
      {1, 1},   {0, 0}, {0, 0}, {0, 0}, {1, 1},   {0, 0}, {0, 0}, {0, 0},
      {1, 1},   {0, 0}, {0, 0}, {0, 0}, {0, 0},   {0, 0}, {0, 0}, {0, 0}};

  // Define freq-domain LTS according to
  // https://standards.ieee.org/standard/802_11a-1999.html
  static constexpr std::complex<float> lts_seq[64] = {
      {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {1, 0},  {1, 0},
      {-1, 0}, {-1, 0}, {1, 0},  {1, 0},  {-1, 0}, {1, 0},  {-1, 0}, {1, 0},
      {1, 0},  {1, 0},  {1, 0},  {1, 0},  {1, 0},  {-1, 0}, {-1, 0}, {1, 0},
      {1, 0},  {-1, 0}, {1, 0},  {-1, 0}, {1, 0},  {1, 0},  {1, 0},  {1, 0},
      {0, 0},  {1, 0},  {-1, 0}, {-1, 0}, {1, 0},  {1, 0},  {-1, 0}, {1, 0},
      {-1, 0}, {1, 0},  {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {1, 0},
      {1, 0},  {-1, 0}, {-1, 0}, {1, 0},  {-1, 0}, {1, 0},  {-1, 0}, {1, 0},
      {1, 0},  {1, 0},  {1, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0}};

  // prime numbers [1,2048]
  static constexpr size_t prime[309] = {
      2,    3,    5,    7,    11,   13,   17,   19,   23,   29,   31,   37,
      41,   43,   47,   53,   59,   61,   67,   71,   73,   79,   83,   89,
      97,   101,  103,  107,  109,  113,  127,  131,  137,  139,  149,  151,
      157,  163,  167,  173,  179,  181,  191,  193,  197,  199,  211,  223,
      227,  229,  233,  239,  241,  251,  257,  263,  269,  271,  277,  281,
      283,  293,  307,  311,  313,  317,  331,  337,  347,  349,  353,  359,
      367,  373,  379,  383,  389,  397,  401,  409,  419,  421,  431,  433,
      439,  443,  449,  457,  461,  463,  467,  479,  487,  491,  499,  503,
      509,  521,  523,  541,  547,  557,  563,  569,  571,  577,  587,  593,
      599,  601,  607,  613,  617,  619,  631,  641,  643,  647,  653,  659,
      661,  673,  677,  683,  691,  701,  709,  719,  727,  733,  739,  743,
      751,  757,  761,  769,  773,  787,  797,  809,  811,  821,  823,  827,
      829,  839,  853,  857,  859,  863,  877,  881,  883,  887,  907,  911,
      919,  929,  937,  941,  947,  953,  967,  971,  977,  983,  991,  997,
      1009, 1013, 1019, 1021, 1031, 1033, 1039, 1049, 1051, 1061, 1063, 1069,
      1087, 1091, 1093, 1097, 1103, 1109, 1117, 1123, 1129, 1151, 1153, 1163,
      1171, 1181, 1187, 1193, 1201, 1213, 1217, 1223, 1229, 1231, 1237, 1249,
      1259, 1277, 1279, 1283, 1289, 1291, 1297, 1301, 1303, 1307, 1319, 1321,
      1327, 1361, 1367, 1373, 1381, 1399, 1409, 1423, 1427, 1429, 1433, 1439,
      1447, 1451, 1453, 1459, 1471, 1481, 1483, 1487, 1489, 1493, 1499, 1511,
      1523, 1531, 1543, 1549, 1553, 1559, 1567, 1571, 1579, 1583, 1597, 1601,
      1607, 1609, 1613, 1619, 1621, 1627, 1637, 1657, 1663, 1667, 1669, 1693,
      1697, 1699, 1709, 1721, 1723, 1733, 1741, 1747, 1753, 1759, 1777, 1783,
      1787, 1789, 1801, 1811, 1823, 1831, 1847, 1861, 1867, 1871, 1873, 1877,
      1879, 1889, 1901, 1907, 1913, 1931, 1933, 1949, 1951, 1973, 1979, 1987,
      1993, 1997, 1999, 2003, 2011, 2017, 2027, 2029, 2039};

  // Row 52 in gold-127
  static constexpr int gold_code[127] = {
      1,  -1, 1,  -1, 1,  -1, 1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
      1,  1,  -1, 1,  1,  1,  -1, 1,  -1, -1, -1, 1,  -1, -1, -1, -1,
      -1, 1,  -1, 1,  -1, -1, 1,  -1, -1, 1,  1,  1,  -1, -1, 1,  1,
      -1, 1,  1,  1,  1,  1,  -1, 1,  -1, 1,  1,  -1, -1, -1, 1,  1,
      1,  1,  -1, 1,  -1, -1, 1,  1,  1,  -1, 1,  1,  -1, -1, -1, 1,
      -1, -1, 1,  -1, -1, -1, -1, -1, -1, -1, 1,  -1, -1, 1,  1,  -1,
      -1, -1, -1, 1,  -1, 1,  1,  1,  1,  -1, 1,  1,  -1, -1, 1,  1,
      -1, 1,  -1, -1, 1,  -1, -1, 1,  -1, -1, 1,  1,  -1, -1, -1};
  /*
        double gold_ifft_re[128] = { -0.5646359, 0.4669951, 0.8769358,
            0.5407985, -0.48144832, -0.88476783, 0.33639774, -0.43609348,
            -0.26278743, 0.6910331, -0.25535262, 0.11774132, 0.46892625,
            0.77644444, -0.14834122, -0.13464923, -0.26617187, 0.1341292,
            0.133574, 0.15594807, -0.057847068, 0.3967621, 0.047606125,
            0.01414329, 0.41560003, 0.12632199, -0.33603117, -0.5669182,
            -0.2004348, 0.55602646, 0.24340886, -0.16611233, 0.7904902,
            -0.42025912, -0.38651145, -0.14808364, -0.27662534, -0.74715126,
            0.5908927, -0.75451213, -0.33933204, 0.36646086, -0.57852495,
            0.10015667, -0.34719938, 0.35134, 0.7383081, -0.3743101,
            -0.53234375, -0.33714586, 0.012157675, -0.399321, -0.3871609,
            0.27705255, 0.4469853, -0.16857521, 0.60894567, -0.04652265,
            0.21421923, 0.014229958, 0.87569416, -0.28046992, 0.64841086,
            0.06317055, -0.037642393, -0.7303067, 0.6826409, -0.091142215,
            -0.080362685, 0.1991867, 0.3268059, 0.6429179, 0.26278743,
            -0.088880904, 0.25250778, 0.2633651, -0.7295981, -0.15740044,
            -0.44250035, -0.0022179564, 0.26617187, -0.33556038, -0.38437498,
            -0.8211783, 0.641319, 0.3527957, -0.062620886, 0.4227164,
            -0.23919682, 0.18401834, -0.14366682, 0.016121548, -0.25830117,
            0.82918876, 0.92221844, 0.31633607, -0.18821196, -0.9082796,
            0.11038142, 0.008659021, -0.18971694, -0.40438867, -0.12019706,
            -0.6811534, 0.33933204, -0.40837204, 0.22615194, 0.38991654,
            0.18199626, -0.1321399, 0.19951832, 0.7384663, 0.53234375,
            0.030798966, 0.40922493, 0.4283689, -0.37271422, 0.22344504,
            0.24096492, 0.1736422, 0.4192076, -0.42793053, 0.37122476,
            -0.008662291, 0.008916863, 0.34757638, -0.35418823, 0.3462311 };

        double gold_ifft_im[128] = { -0.5646359, 0.3462311, -0.35418823,
            0.34757638, 0.008916863, -0.008662291, 0.37122476, -0.42793053,
            0.4192076, 0.1736422, 0.24096492, 0.22344504, -0.37271422,
            0.4283689, 0.40922493, 0.030798966, 0.53234375, 0.7384663,
            0.19951832, -0.1321399, 0.18199626, 0.38991654, 0.22615194,
            -0.40837204, 0.33933204, -0.6811534, -0.12019706, -0.40438867,
            -0.18971694, 0.008659021, 0.11038142, -0.9082796, -0.18821196,
            0.31633607, 0.92221844, 0.82918876, -0.25830117, 0.016121548,
            -0.14366682, 0.18401834, -0.23919682, 0.4227164, -0.062620886,
            0.3527957, 0.641319, -0.8211783, -0.38437498, -0.33556038,
            0.26617187, -0.0022179564, -0.44250035, -0.15740044, -0.7295981,
            0.2633651, 0.25250778, -0.088880904, 0.26278743, 0.6429179,
            0.3268059, 0.1991867, -0.080362685, -0.091142215, 0.6826409,
            -0.7303067, -0.037642393, 0.06317055, 0.64841086, -0.28046992,
            0.87569416, 0.014229958, 0.21421923, -0.04652265, 0.60894567,
            -0.16857521, 0.4469853, 0.27705255, -0.3871609, -0.399321,
            0.012157675, -0.33714586, -0.53234375, -0.3743101, 0.7383081,
            0.35134, -0.34719938, 0.10015667, -0.57852495, 0.36646086,
            -0.33933204, -0.75451213, 0.5908927, -0.74715126, -0.27662534,
            -0.14808364, -0.38651145, -0.42025912, 0.7904902, -0.16611233,
            0.24340886, 0.55602646, -0.2004348, -0.5669182, -0.33603117,
            0.12632199, 0.41560003, 0.01414329, 0.047606125, 0.3967621,
            -0.057847068, 0.15594807, 0.133574, 0.1341292, -0.26617187,
            -0.13464923, -0.14834122, 0.77644444, 0.46892625, 0.11774132,
            -0.25535262, 0.6910331, -0.26278743, -0.43609348, 0.33639774,
            -0.88476783, -0.48144832, 0.5407985, 0.8769358, 0.4669951 };
	*/
};
