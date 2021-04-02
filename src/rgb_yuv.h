
#ifndef RGB_YUV
#define RGB_YUV

/*
 * RGB to YUYV conversion 
 */

uint8_t mult_112[256] = {128, 128, 129, 129, 130, 130, 131, 131, 132, 132, 132, 133, 133,
                           134, 134, 135, 135, 135, 136, 136, 137, 137, 138, 138, 139, 139,
                           139, 140, 140, 141, 141, 142, 142, 142, 143, 143, 144, 144, 145,
                           145, 146, 146, 146, 147, 147, 148, 148, 149, 149, 149, 150, 150,
                           151, 151, 152, 152, 153, 153, 153, 154, 154, 155, 155, 156, 156,
                           156, 157, 157, 158, 158, 159, 159, 160, 160, 160, 161, 161, 162,
                           162, 163, 163, 163, 164, 164, 165, 165, 166, 166, 167, 167, 167,
                           168, 168, 169, 169, 170, 170, 170, 171, 171, 172, 172, 173, 173,
                           174, 174, 174, 175, 175, 176, 176, 177, 177, 177, 178, 178, 179,
                           179, 180, 180, 181, 181, 181, 182, 182, 183, 183, 184, 184, 184,
                           185, 185, 186, 186, 187, 187, 188, 188, 188, 189, 189, 190, 190,
                           191, 191, 191, 192, 192, 193, 193, 194, 194, 195, 195, 195, 196,
                           196, 197, 197, 198, 198, 198, 199, 199, 200, 200, 201, 201, 202,
                           202, 202, 203, 203, 204, 204, 205, 205, 205, 206, 206, 207, 207,
                           208, 208, 209, 209, 209, 210, 210, 211, 211, 212, 212, 212, 213,
                           213, 214, 214, 215, 215, 216, 216, 216, 217, 217, 218, 218, 219,
                           219, 219, 220, 220, 221, 221, 222, 222, 223, 223, 223, 224, 224,
                           225, 225, 226, 226, 226, 227, 227, 228, 228, 229, 229, 230, 230,
                           230, 231, 231, 232, 232, 233, 233, 233, 234, 234, 235, 235, 236,
                           236, 237, 237, 237, 238, 238, 239, 239, 240};

uint8_t mult_94[256] = {0, 0, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8,
                          8, 9, 9, 10, 10, 10, 11, 11, 11, 12, 12, 12, 13, 13, 14, 14, 14, 15,
                          15, 15, 16, 16, 17, 17, 17, 18, 18, 18, 19, 19, 19, 20, 20, 21, 21,
                          21, 22, 22, 22, 23, 23, 24, 24, 24, 25, 25, 25, 26, 26, 26, 27, 27,
                          28, 28, 28, 29, 29, 29, 30, 30, 30, 31, 31, 32, 32, 32, 33, 33, 33,
                          34, 34, 35, 35, 35, 36, 36, 36, 37, 37, 37, 38, 38, 39, 39, 39, 40,
                          40, 40, 41, 41, 41, 42, 42, 43, 43, 43, 44, 44, 44, 45, 45, 46, 46,
                          46, 47, 47, 47, 48, 48, 48, 49, 49, 50, 50, 50, 51, 51, 51, 52, 52,
                          53, 53, 53, 54, 54, 54, 55, 55, 55, 56, 56, 57, 57, 57, 58, 58, 58,
                          59, 59, 59, 60, 60, 61, 61, 61, 62, 62, 62, 63, 63, 64, 64, 64, 65,
                          65, 65, 66, 66, 66, 67, 67, 68, 68, 68, 69, 69, 69, 70, 70, 71, 71,
                          71, 72, 72, 72, 73, 73, 73, 74, 74, 75, 75, 75, 76, 76, 76, 77, 77,
                          77, 78, 78, 79, 79, 79, 80, 80, 80, 81, 81, 82, 82, 82, 83, 83, 83,
                          84, 84, 84, 85, 85, 86, 86, 86, 87, 87, 87, 88, 88, 88, 89, 89, 90,
                          90, 90, 91, 91, 91, 92, 92, 93, 93, 93, 94};

uint8_t mult_18[256] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
                          2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                          3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5,
                          5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
                          6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8,
                          8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10,
                          10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11,
                          11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12,
                          12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
                          13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
                          14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16,
                          16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17,
                          17, 17, 17, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18};

uint8_t mult_38[256] = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3,
                          3, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 7, 7,
                          7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10,
                          10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12,
                          13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15,
                          15, 15, 15, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 18,
                          18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20,
                          20, 20, 20, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 23,
                          23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25,
                          25, 25, 26, 26, 26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 28,
                          28, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29, 29, 30, 30, 30, 30, 30,
                          30, 30, 31, 31, 31, 31, 31, 31, 31, 32, 32, 32, 32, 32, 32, 33, 33,
                          33, 33, 33, 33, 33, 34, 34, 34, 34, 34, 34, 34, 35, 35, 35, 35, 35,
                          35, 35, 36, 36, 36, 36, 36, 36, 37, 37, 37, 37, 37, 37, 37, 38, 38,
                          38};

uint8_t mult_74[256] = {0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6,
                          7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 12, 12, 12,
                          12, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 16, 16, 16, 16, 17, 17, 17,
                          18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22,
                          23, 23, 23, 23, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 27, 27, 27,
                          27, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 31, 31, 31, 32, 32, 32,
                          32, 33, 33, 33, 34, 34, 34, 34, 35, 35, 35, 36, 36, 36, 36, 37, 37,
                          37, 38, 38, 38, 38, 39, 39, 39, 40, 40, 40, 40, 41, 41, 41, 42, 42,
                          42, 42, 43, 43, 43, 44, 44, 44, 45, 45, 45, 45, 46, 46, 46, 47, 47,
                          47, 47, 48, 48, 48, 49, 49, 49, 49, 50, 50, 50, 51, 51, 51, 51, 52,
                          52, 52, 53, 53, 53, 53, 54, 54, 54, 55, 55, 55, 56, 56, 56, 56, 57,
                          57, 57, 58, 58, 58, 58, 59, 59, 59, 60, 60, 60, 60, 61, 61, 61, 62,
                          62, 62, 62, 63, 63, 63, 64, 64, 64, 64, 65, 65, 65, 66, 66, 66, 66,
                          67, 67, 67, 68, 68, 68, 69, 69, 69, 69, 70, 70, 70, 71, 71, 71, 71,
                          72, 72, 72, 73, 73, 73, 73, 74};

#define rgb2yvyu(r1, g1, b1, r2, g2, b2)                                   \
    ({                                                                     \
        uint8_t r12 = (r1 + r2) >> 1;                                      \
        uint8_t g12 = (g1 + g2) >> 1;                                      \
        uint8_t b12 = (b1 + b2) >> 1;                                      \
        (uint8_t)((r1 >> 2) + (g1 >> 1) + (b1 >> 3) + 16) +                \
            ((mult_112[r12] - mult_94[g12] - mult_18[b12]) << 8) +   \
            ((uint8_t)((r2 >> 2) + (g2 >> 1) + (b2 >> 3) + 16) << 16) +    \
            (((mult_112[b12] - mult_38[r12] - mult_74[g12])) << 24); \
    })

#define rgb2yvyu_opt(r1, g1, b1, r2, g2, b2)                               \
    ({                                                                     \
        uint8_t r12 = (r1 + r2);                                           \
        uint8_t g12 = (g1 + g2);                                           \
        uint8_t b12 = (b1 + b2);                                           \
        (uint8_t)((r1 >> 1) + g1 + (b1 >> 2) + 16) +                       \
            ((mult_112[r12] - mult_94[g12] - mult_18[b12]) << 8) +   \
            ((uint8_t)((r2 >> 1) + g2 + (b2 >> 2) + 16) << 16) +           \
            (((mult_112[b12] - mult_38[r12] - mult_74[g12])) << 24); \
    })

#endif // end RGB_YUV
