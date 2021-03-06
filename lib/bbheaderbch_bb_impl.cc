/* -*- c++ -*- */
/* 
 * Copyright 2017 Ron Economos.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include "bbheaderbch_bb_impl.h"
#include <stdio.h>

namespace gr {
  namespace dvbt2ll {

    bbheaderbch_bb::sptr
    bbheaderbch_bb::make(dvbt2_framesize_t framesize, dvbt2_code_rate_t rate, dvbt2_inputmode_t mode, dvbt2_inband_t inband, int fecblocks, int tsrate)
    {
      return gnuradio::get_initial_sptr
        (new bbheaderbch_bb_impl(framesize, rate, mode, inband, fecblocks, tsrate));
    }

    /*
     * The private constructor
     */
    bbheaderbch_bb_impl::bbheaderbch_bb_impl(dvbt2_framesize_t framesize, dvbt2_code_rate_t rate, dvbt2_inputmode_t mode, dvbt2_inband_t inband, int fecblocks, int tsrate)
      : gr::block("bbheaderbch_bb",
              gr::io_signature::make(1, 1, sizeof(unsigned char)),
              gr::io_signature::make(1, 1, sizeof(unsigned char)))
    {
      count = 0;
      crc = 0x0;
      frame_size_type = framesize;
      BBHeader *f = &m_format[0].bb_header;
      if (framesize == FECFRAME_NORMAL) {
        frame_size = FRAME_SIZE_NORMAL;
        switch (rate) {
          case C1_2:
            kbch = 32208;
            nbch = 32400;
            q_val = 90;
            bch_code = BCH_CODE_N12;
            break;
          case C3_5:
            kbch = 38688;
            nbch = 38880;
            q_val = 72;
            bch_code = BCH_CODE_N12;
            break;
          case C2_3:
            kbch = 43040;
            nbch = 43200;
            q_val = 60;
            bch_code = BCH_CODE_N10;
            break;
          case C3_4:
            kbch = 48408;
            nbch = 48600;
            q_val = 45;
            bch_code = BCH_CODE_N12;
            break;
          case C4_5:
            kbch = 51648;
            nbch = 51840;
            q_val = 36;
            bch_code = BCH_CODE_N12;
            break;
          case C5_6:
            kbch = 53840;
            nbch = 54000;
            q_val = 30;
            bch_code = BCH_CODE_N10;
            break;
          default:
            kbch = 0;
            break;
        }
      }
      else {
        frame_size = FRAME_SIZE_SHORT;
        switch (rate) {
          case C1_3:
            kbch = 5232;
            nbch = 5400;
            q_val = 30;
            bch_code = BCH_CODE_S12;
            break;
          case C2_5:
            kbch = 6312;
            nbch = 6480;
            q_val = 27;
            bch_code = BCH_CODE_S12;
            break;
          case C1_2:
            kbch = 7032;
            nbch = 7200;
            q_val = 25;
            bch_code = BCH_CODE_S12;
            break;
          case C3_5:
            kbch = 9552;
            nbch = 9720;
            q_val = 18;
            bch_code = BCH_CODE_S12;
            break;
          case C2_3:
            kbch = 10632;
            nbch = 10800;
            q_val = 15;
            bch_code = BCH_CODE_S12;
            break;
          case C3_4:
            kbch = 11712;
            nbch = 11880;
            q_val = 12;
            bch_code = BCH_CODE_S12;
            break;
          case C4_5:
            kbch = 12432;
            nbch = 12600;
            q_val = 10;
            bch_code = BCH_CODE_S12;
            break;
          case C5_6:
            kbch = 13152;
            nbch = 13320;
            q_val = 8;
            bch_code = BCH_CODE_S12;
            break;
          default:
            kbch = 0;
            break;
        }
      }

      switch (bch_code) {
        case BCH_CODE_N12:
          num_parity_bits = 192;
          break;
        case BCH_CODE_N10:
          num_parity_bits = 160;
          break;
        case BCH_CODE_N8:
          num_parity_bits = 128;
          break;
        case BCH_CODE_S12:
          num_parity_bits = 168;
          break;
      }

      f->ts_gs   = TS_GS_TRANSPORT;
      f->sis_mis = SIS_MIS_SINGLE;
      f->ccm_acm = CCM;
      f->issyi   = ISSYI_NOT_ACTIVE;
      f->npd     = NPD_NOT_ACTIVE;
      if (mode == INPUTMODE_NORMAL) {
        f->upl  = 188 * 8;
        f->dfl  = kbch - 80;
        f->sync = 0x47;
      }
      else {
        f->upl  = 0;
        f->dfl  = kbch - 80;
        f->sync = 0;
      }
      f->ro = 0;

      build_crc8_table();
      init_bb_randomiser();
      bch_poly_build_tables();
      code_rate = rate;
      ldpc_lookup_generate();
      input_mode = mode;
      inband_type_b = inband;
      fec_blocks = fecblocks;
      fec_block = 0;
      ts_rate = tsrate;
      extra = (((kbch - 80) / 8) / 187) + 1;
      set_output_multiple(nbch);
    }

    /*
     * Our virtual destructor.
     */
    bbheaderbch_bb_impl::~bbheaderbch_bb_impl()
    {
      delete[] ldpc_lut[0];
      delete[] ldpc_lut;
    }

    void
    bbheaderbch_bb_impl::forecast (int noutput_items, gr_vector_int &ninput_items_required)
    {
      if (input_mode == INPUTMODE_NORMAL) {
        ninput_items_required[0] = ((noutput_items - 80 - (nbch - kbch)) / 8);
      }
      else {
        ninput_items_required[0] = ((noutput_items - 80 - (nbch - kbch)) / 8) + extra;
      }
    }

#define CRC_POLY 0xAB
// Reversed
#define CRC_POLYR 0xD5

    void
    bbheaderbch_bb_impl::build_crc8_table(void)
    {
      int r, crc;

      for (int i = 0; i < 256; i++) {
        r = i;
        crc = 0;
        for (int j = 7; j >= 0; j--) {
          if ((r & (1 << j) ? 1 : 0) ^ ((crc & 0x80) ? 1 : 0)) {
            crc = (crc << 1) ^ CRC_POLYR;
          }
          else {
            crc <<= 1;
          }
        }
        crc_tab[i] = crc;
      }
    }

    /*
     * MSB is sent first
     *
     * The polynomial has been reversed
     */
    int
    bbheaderbch_bb_impl::add_crc8_bits(unsigned char *in, int length)
    {
      int crc = 0;
      int b;
      int i = 0;

      for (int n = 0; n < length; n++) {
        b = in[i++] ^ (crc & 0x01);
        crc >>= 1;
        if (b) {
          crc ^= CRC_POLY;
        }
      }

      if (input_mode == INPUTMODE_HIEFF) {
        crc ^= 0x80;
      }

      for (int n = 0; n < 8; n++) {
        in[i++] = (crc & (1 << n)) ? 1 : 0;
      }
      return 8;// Length of CRC
    }

    void
    bbheaderbch_bb_impl::add_bbheader(unsigned char *out, int count, int padding)
    {
      int temp, m_frame_offset_bits;
      unsigned char *m_frame = out;
      BBHeader *h = &m_format[0].bb_header;

      m_frame[0] = h->ts_gs >> 1;
      m_frame[1] = h->ts_gs & 1;
      m_frame[2] = h->sis_mis;
      m_frame[3] = h->ccm_acm;
      m_frame[4] = h->issyi & 1;
      m_frame[5] = h->npd & 1;
      m_frame[6] = h->ro >> 1;
      m_frame[7] = h->ro & 1;
      m_frame_offset_bits = 8;
      if (h->sis_mis == SIS_MIS_MULTIPLE) {
        temp = h->isi;
        for (int n = 7; n >= 0; n--) {
          m_frame[m_frame_offset_bits++] = temp & (1 << n) ? 1 : 0;
        }
      }
      else {
        for (int n = 7; n >= 0; n--) {
          m_frame[m_frame_offset_bits++] = 0;
        }
      }
      temp = h->upl;
      for (int n = 15; n >= 0; n--) {
        m_frame[m_frame_offset_bits++] = temp & (1 << n) ? 1 : 0;
      }
      temp = h->dfl - padding;
      for (int n = 15; n >= 0; n--) {
        m_frame[m_frame_offset_bits++] = temp & (1 << n) ? 1 : 0;
      }
      temp = h->sync;
      for (int n = 7; n >= 0; n--) {
        m_frame[m_frame_offset_bits++] = temp & (1 << n) ? 1 : 0;
      }
      // Calculate syncd, this should point to the MSB of the CRC
      temp = count;
      if (temp == 0) {
        temp = count;
      }
      else {
        temp = (188 - count) * 8;
      }
      for (int n = 15; n >= 0; n--) {
        m_frame[m_frame_offset_bits++] = temp & (1 << n) ? 1 : 0;
      }
      // Add CRC to BB header, at end
      int len = BB_HEADER_LENGTH_BITS;
      m_frame_offset_bits += add_crc8_bits(m_frame, len);
    }

    void
    bbheaderbch_bb_impl::add_inband_type_b(unsigned char *out, int ts_rate)
    {
      int temp, m_frame_offset_bits;
      unsigned char *m_frame = out;

      m_frame[0] = 0;
      m_frame[1] = 1;
      m_frame_offset_bits = 2;
      for (int n = 30; n >= 0; n--) {
        m_frame[m_frame_offset_bits++] = 0;
      }
      for (int n = 21; n >= 0; n--) {
        m_frame[m_frame_offset_bits++] = 0;
      }
      for (int n = 1; n >= 0; n--) {
        m_frame[m_frame_offset_bits++] = 0;
      }
      for (int n = 9; n >= 0; n--) {
        m_frame[m_frame_offset_bits++] = 0;
      }
      temp = ts_rate;
      for (int n = 26; n >= 0; n--) {
        m_frame[m_frame_offset_bits++] = temp & (1 << n) ? 1 : 0;
      }
      for (int n = 9; n >= 0; n--) {
        m_frame[m_frame_offset_bits++] = 0;
      }
    }

    void
    bbheaderbch_bb_impl::init_bb_randomiser(void)
    {
      int sr = 0x4A80;
      for (int i = 0; i < FRAME_SIZE_NORMAL; i++) {
        int b = ((sr) ^ (sr >> 1)) & 1;
        bb_randomise[i] = b;
        sr >>= 1;
        if(b) {
          sr |= 0x4000;
        }
      }
    }

    /*
     * Polynomial calculation routines
     * multiply polynomials
     */
    int
    bbheaderbch_bb_impl::poly_mult(const int *ina, int lena, const int *inb, int lenb, int *out)
    {
      memset(out, 0, sizeof(int) * (lena + lenb));

      for (int i = 0; i < lena; i++) {
        for (int j = 0; j < lenb; j++) {
          if (ina[i] * inb[j] > 0 ) {
            out[i + j]++;    // count number of terms for this pwr of x
          }
        }
      }
      int max = 0;
      for (int i = 0; i < lena + lenb; i++) {
        out[i] = out[i] & 1;    // If even ignore the term
        if(out[i]) {
          max = i;
        }
      }
      // return the size of array to house the result.
      return max + 1;
    }

    //precalculate the crc from: http://www.sunshine2k.de/articles/coding/crc/understanding_crc.html - cf. CRC-32 Lookup
    void
    bbheaderbch_bb_impl::calculate_crc_table(void)
    {
      for (int divident = 0; divident < 256; divident++) { /* iterate over all possible input byte values 0 - 255 */
        std::bitset<MAX_BCH_PARITY_BITS> curByte(divident);
        curByte <<= num_parity_bits - 8;

        for (unsigned char bit = 0; bit < 8; bit++) {
          if ((curByte[num_parity_bits - 1]) != 0) {
            curByte <<= 1;
            curByte ^= polynome;
          }
          else {
            curByte <<= 1;
          }
        }
        crc_table[divident] = curByte;
      }
    }

#define COPY_BCH_POLYNOME \
for (unsigned int i = 0; i < num_parity_bits; i ++) {\
  polynome[i] = polyout[0][i];\
}

    void
    bbheaderbch_bb_impl::bch_poly_build_tables(void)
    {
      // Normal polynomials
      const int polyn01[]={1,0,1,1,0,1,0,0,0,0,0,0,0,0,0,0,1};
      const int polyn02[]={1,1,0,0,1,1,1,0,1,0,0,0,0,0,0,0,1};
      const int polyn03[]={1,0,1,1,1,1,0,1,1,1,1,1,0,0,0,0,1};
      const int polyn04[]={1,0,1,0,1,0,1,0,0,1,0,1,1,0,1,0,1};
      const int polyn05[]={1,1,1,1,0,1,0,0,1,1,1,1,1,0,0,0,1};
      const int polyn06[]={1,0,1,0,1,1,0,1,1,1,1,0,1,1,1,1,1};
      const int polyn07[]={1,0,1,0,0,1,1,0,1,1,1,1,0,1,0,1,1};
      const int polyn08[]={1,1,1,0,0,1,1,0,1,1,0,0,1,1,1,0,1};
      const int polyn09[]={1,0,0,0,0,1,0,1,0,1,1,1,0,0,0,0,1};
      const int polyn10[]={1,1,1,0,0,1,0,1,1,0,1,0,1,1,1,0,1};
      const int polyn11[]={1,0,1,1,0,1,0,0,0,1,0,1,1,1,0,0,1};
      const int polyn12[]={1,1,0,0,0,1,1,1,0,1,0,1,1,0,0,0,1};

      // Short polynomials
      const int polys01[]={1,1,0,1,0,1,0,0,0,0,0,0,0,0,1};
      const int polys02[]={1,0,0,0,0,0,1,0,1,0,0,1,0,0,1};
      const int polys03[]={1,1,1,0,0,0,1,0,0,1,1,0,0,0,1};
      const int polys04[]={1,0,0,0,1,0,0,1,1,0,1,0,1,0,1};
      const int polys05[]={1,0,1,0,1,0,1,0,1,1,0,1,0,1,1};
      const int polys06[]={1,0,0,1,0,0,0,1,1,1,0,0,0,1,1};
      const int polys07[]={1,0,1,0,0,1,1,1,0,0,1,1,0,1,1};
      const int polys08[]={1,0,0,0,0,1,0,0,1,1,1,1,0,0,1};
      const int polys09[]={1,1,1,1,0,0,0,0,0,1,1,0,0,0,1};
      const int polys10[]={1,0,0,1,0,0,1,0,0,1,0,1,1,0,1};
      const int polys11[]={1,0,0,0,1,0,0,0,0,0,0,1,1,0,1};
      const int polys12[]={1,1,1,1,0,1,1,1,1,0,1,0,0,1,1};

      int len;
      int polyout[2][200];

      switch (bch_code) {
        case BCH_CODE_N12:
        case BCH_CODE_N10:
        case BCH_CODE_N8:
          len = poly_mult(polyn01, 17, polyn02,    17,  polyout[0]);
          len = poly_mult(polyn03, 17, polyout[0], len, polyout[1]);
          len = poly_mult(polyn04, 17, polyout[1], len, polyout[0]);
          len = poly_mult(polyn05, 17, polyout[0], len, polyout[1]);
          len = poly_mult(polyn06, 17, polyout[1], len, polyout[0]);
          len = poly_mult(polyn07, 17, polyout[0], len, polyout[1]);
          len = poly_mult(polyn08, 17, polyout[1], len, polyout[0]);
          if (bch_code == BCH_CODE_N8) {
            COPY_BCH_POLYNOME
          }

          len = poly_mult(polyn09, 17, polyout[0], len, polyout[1]);
          len = poly_mult(polyn10, 17, polyout[1], len, polyout[0]);
          if (bch_code == BCH_CODE_N10) {
            COPY_BCH_POLYNOME
          }

          len = poly_mult(polyn11, 17, polyout[0], len, polyout[1]);
          len = poly_mult(polyn12, 17, polyout[1], len, polyout[0]);
          if (bch_code == BCH_CODE_N12) {
            COPY_BCH_POLYNOME
          }
          break;

        case BCH_CODE_S12:
          len = poly_mult(polys01, 15, polys02,    15,  polyout[0]);
          len = poly_mult(polys03, 15, polyout[0], len, polyout[1]);
          len = poly_mult(polys04, 15, polyout[1], len, polyout[0]);
          len = poly_mult(polys05, 15, polyout[0], len, polyout[1]);
          len = poly_mult(polys06, 15, polyout[1], len, polyout[0]);
          len = poly_mult(polys07, 15, polyout[0], len, polyout[1]);
          len = poly_mult(polys08, 15, polyout[1], len, polyout[0]);
          len = poly_mult(polys09, 15, polyout[0], len, polyout[1]);
          len = poly_mult(polys10, 15, polyout[1], len, polyout[0]);
          len = poly_mult(polys11, 15, polyout[0], len, polyout[1]);
          len = poly_mult(polys12, 15, polyout[1], len, polyout[0]);
          COPY_BCH_POLYNOME
          break;
      }
      calculate_crc_table();
    }

    void
    bbheaderbch_bb_impl::bch_calculate(unsigned char *in)
    {
      // We can use a 192 bits long bitset, all higher bits not used by the bch will just be ignored
      std::bitset<MAX_BCH_PARITY_BITS> parity_bits;
      unsigned char b, temp;

      for (int j = 0; j < (int)kbch / 8; j++) {
        b = 0;
        // calculate the crc using the lookup table, cf. http://www.sunshine2k.de/articles/coding/crc/understanding_crc.html
        for (int e = 0; e < 8; e++) {
          temp = *in++;
          b |= temp << (7 - e);
        }

        unsigned long msB_CRC = (parity_bits >> (num_parity_bits - 8)).to_ulong();
        /* XOR-in next input byte into MSB of crc and get this MSB, that's our new intermediate divident */
        unsigned char pos = (unsigned char)(msB_CRC ^ b);
        /* Shift out the MSB used for division per lookuptable and XOR with the remainder */
        parity_bits = (parity_bits << 8) ^ crc_table[pos];
      }

      // Now add the parity bits to the output
      for (unsigned int n = 0; n < num_parity_bits; n++) {
        *in++ = (char) parity_bits[num_parity_bits - 1];
        parity_bits <<= 1;
      }
    }

#define LDPC_BF(TABLE_NAME, ROWS) \
for (int row = 0; row < ROWS; row++) { /* count the entries in the table */ \
  max_lut_arraysize += TABLE_NAME[row][0]; \
} \
max_lut_arraysize *= 360; /* 360 bits per table entry */ \
max_lut_arraysize /= pbits; /* spread over all parity bits */ \
max_lut_arraysize += 2; /* 1 for the size at the start of the array, one as buffer */ \
\
/* Allocate a 2D Array with pbits * max_lut_arraysize
 * while preserving two-subscript access
 * see https://stackoverflow.com/questions/29375797/copy-2d-array-using-memcpy/29375830#29375830
 */ \
ldpc_lut = new int*[pbits]; \
ldpc_lut[0] = new int[pbits * max_lut_arraysize]; \
ldpc_lut[0][0] = 1; \
for (int i = 1; i < pbits; i++) { \
  ldpc_lut[i] = ldpc_lut[i-1] + max_lut_arraysize; \
  ldpc_lut[i][0] = 1; \
} \
for (int row = 0; row < ROWS; row++) { \
  for (int n = 0; n < 360; n++) { \
    for (int col = 1; col <= TABLE_NAME[row][0]; col++) { \
      int current_pbit = (TABLE_NAME[row][col] + (n * q)) % pbits; \
      ldpc_lut[current_pbit][ldpc_lut[current_pbit][0]] = im; \
      ldpc_lut[current_pbit][0]++; \
    } \
    im++; \
  } \
}

    /*
     * fill the lookup table, for each paritybit it contains
     * {number of infobits, infobit1, infobit2, ... ]
     * maximum number of infobits is calculated using the entries
     * in the ldpc tables
     */
    void
    bbheaderbch_bb_impl::ldpc_lookup_generate(void)
    {
      int im = 0;
      int pbits = frame_size - nbch;    //number of parity bits
      int q = q_val;
      int max_lut_arraysize = 0;

      if (frame_size_type == FECFRAME_NORMAL) {
        if (code_rate == C1_2) {
          LDPC_BF(ldpc_tab_1_2N,  90);
        }
        if (code_rate == C3_5) {
          LDPC_BF(ldpc_tab_3_5N,  108);
        }
        if (code_rate == C2_3) {
          LDPC_BF(ldpc_tab_2_3N_DVBT2, 120);
        }
        if (code_rate == C3_4) {
          LDPC_BF(ldpc_tab_3_4N,  135);
        }
        if (code_rate == C4_5) {
          LDPC_BF(ldpc_tab_4_5N,  144);
        }
        if (code_rate == C5_6) {
          LDPC_BF(ldpc_tab_5_6N,  150);
        }
      }
      else {
        if (code_rate == C1_3) {
          LDPC_BF(ldpc_tab_1_3S, 15);
        }
        if (code_rate == C2_5) {
          LDPC_BF(ldpc_tab_2_5S, 18);
        }
        if (code_rate == C1_2) {
          LDPC_BF(ldpc_tab_1_2S, 20);
        }
        if (code_rate == C3_5) {
          LDPC_BF(ldpc_tab_3_5S_DVBT2, 27);
        }
        if (code_rate == C2_3) {
          LDPC_BF(ldpc_tab_2_3S, 30);
        }
        if (code_rate == C3_4) {
          LDPC_BF(ldpc_tab_3_4S, 33);
        }
        if (code_rate == C4_5) {
          LDPC_BF(ldpc_tab_4_5S, 35);
        }
        if (code_rate == C5_6) {
          LDPC_BF(ldpc_tab_5_6S, 37);
        }
      }
    }

    void
    bbheaderbch_bb_impl::ldpc_calculate(unsigned char *in)
    {
      unsigned char *p;
      int plen = frame_size - nbch;
      p = &in[nbch];

      // First zero all the parity bits
      memset(p, 0, sizeof(unsigned char) * plen);

      // now do the parity checking
      for (int i_p = 0; i_p < plen; i_p++) {
        unsigned char pbit = 0;
        for (int i_d = 1; i_d < ldpc_lut[i_p][0]; i_d++) {
          pbit ^= in[ldpc_lut[i_p][i_d]];
        }
        p[i_p] = pbit;
      }
      for (int j = 1; j < plen; j++) {
        p[j] ^= p[j-1];
      }
    }

    int
    bbheaderbch_bb_impl::general_work (int noutput_items,
                       gr_vector_int &ninput_items,
                       gr_vector_const_void_star &input_items,
                       gr_vector_void_star &output_items)
    {
      const unsigned char *in = (const unsigned char *) input_items[0];
      unsigned char *out = (unsigned char *) output_items[0];
      int consumed = 0;
      int offset;
      int padding;
      unsigned char b;

      for (int i = 0; i < noutput_items; i += nbch) {
        offset = 0;
        if (fec_block == 0 && inband_type_b == TRUE) {
          padding = 104;
        }
        else {
          padding = 0;
        }
        add_bbheader(&out[offset], count, padding);
        offset = offset + 80;

        if (input_mode == INPUTMODE_HIEFF) {
          for (int j = 0; j < (int)((kbch - 80 - padding) / 8); j++) {
            if (count == 0) {
              if (*in != 0x47) {
                GR_LOG_WARN(d_logger, "Transport Stream sync error!");
              }
              j--;
              in++;
            }
            else {
              b = *in++;
              for (int n = 7; n >= 0; n--) {
                out[offset++] = b & (1 << n) ? 1 : 0;
              }
            }
            count = (count + 1) % 188;
            consumed++;
          }
          if (fec_block == 0 && inband_type_b == TRUE) {
            add_inband_type_b(&out[offset], ts_rate);
            offset = offset + 104;
          }
          for (int j = 0; j < (int)kbch; ++j) {
            out[j] = out[j] ^ bb_randomise[j];
          }
          bch_calculate(out);
//          ldpc_calculate(out);
        }
        else {
          for (int j = 0; j < (int)((kbch - 80 - padding) / 8); j++) {
            if (count == 0) {
              if (*in != 0x47) {
                GR_LOG_WARN(d_logger, "Transport Stream sync error!");
              }
              in++;
              b = crc;
              crc = 0;
            }
            else {
              b = *in++;
              crc = crc_tab[b ^ crc];
            }
            count = (count + 1) % 188;
            consumed++;
            for (int n = 7; n >= 0; n--) {
              out[offset++] = b & (1 << n) ? 1 : 0;
            }
          }
          if (fec_block == 0 && inband_type_b == TRUE) {
            add_inband_type_b(&out[offset], ts_rate);
            offset = offset + 104;
          }
          for (int j = 0; j < (int)kbch; ++j) {
            out[j] = out[j] ^ bb_randomise[j];
          }
          bch_calculate(out);
//          ldpc_calculate(out);
        }
        if (inband_type_b == TRUE) {
          fec_block = (fec_block + 1) % fec_blocks;
        }
        out += nbch;
      }

      // Tell runtime system how many input items we consumed on
      // each input stream.
      consume_each (consumed);

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }

    const int bbheaderbch_bb_impl::ldpc_tab_1_2N[90][9]=
    {
      {8,54,9318,14392,27561,26909,10219,2534,8597},
      {8,55,7263,4635,2530,28130,3033,23830,3651},
      {8,56,24731,23583,26036,17299,5750,792,9169},
      {8,57,5811,26154,18653,11551,15447,13685,16264},
      {8,58,12610,11347,28768,2792,3174,29371,12997},
      {8,59,16789,16018,21449,6165,21202,15850,3186},
      {8,60,31016,21449,17618,6213,12166,8334,18212},
      {8,61,22836,14213,11327,5896,718,11727,9308},
      {8,62,2091,24941,29966,23634,9013,15587,5444},
      {8,63,22207,3983,16904,28534,21415,27524,25912},
      {8,64,25687,4501,22193,14665,14798,16158,5491},
      {8,65,4520,17094,23397,4264,22370,16941,21526},
      {8,66,10490,6182,32370,9597,30841,25954,2762},
      {8,67,22120,22865,29870,15147,13668,14955,19235},
      {8,68,6689,18408,18346,9918,25746,5443,20645},
      {8,69,29982,12529,13858,4746,30370,10023,24828},
      {8,70,1262,28032,29888,13063,24033,21951,7863},
      {8,71,6594,29642,31451,14831,9509,9335,31552},
      {8,72,1358,6454,16633,20354,24598,624,5265},
      {8,73,19529,295,18011,3080,13364,8032,15323},
      {8,74,11981,1510,7960,21462,9129,11370,25741},
      {8,75,9276,29656,4543,30699,20646,21921,28050},
      {8,76,15975,25634,5520,31119,13715,21949,19605},
      {8,77,18688,4608,31755,30165,13103,10706,29224},
      {8,78,21514,23117,12245,26035,31656,25631,30699},
      {8,79,9674,24966,31285,29908,17042,24588,31857},
      {8,80,21856,27777,29919,27000,14897,11409,7122},
      {8,81,29773,23310,263,4877,28622,20545,22092},
      {8,82,15605,5651,21864,3967,14419,22757,15896},
      {8,83,30145,1759,10139,29223,26086,10556,5098},
      {8,84,18815,16575,2936,24457,26738,6030,505},
      {8,85,30326,22298,27562,20131,26390,6247,24791},
      {8,86,928,29246,21246,12400,15311,32309,18608},
      {8,87,20314,6025,26689,16302,2296,3244,19613},
      {8,88,6237,11943,22851,15642,23857,15112,20947},
      {8,89,26403,25168,19038,18384,8882,12719,7093},
      {3,0,14567,24965,0,0,0,0,0},
      {3,1,3908,100,0,0,0,0,0},
      {3,2,10279,240,0,0,0,0,0},
      {3,3,24102,764,0,0,0,0,0},
      {3,4,12383,4173,0,0,0,0,0},
      {3,5,13861,15918,0,0,0,0,0},
      {3,6,21327,1046,0,0,0,0,0},
      {3,7,5288,14579,0,0,0,0,0},
      {3,8,28158,8069,0,0,0,0,0},
      {3,9,16583,11098,0,0,0,0,0},
      {3,10,16681,28363,0,0,0,0,0},
      {3,11,13980,24725,0,0,0,0,0},
      {3,12,32169,17989,0,0,0,0,0},
      {3,13,10907,2767,0,0,0,0,0},
      {3,14,21557,3818,0,0,0,0,0},
      {3,15,26676,12422,0,0,0,0,0},
      {3,16,7676,8754,0,0,0,0,0},
      {3,17,14905,20232,0,0,0,0,0},
      {3,18,15719,24646,0,0,0,0,0},
      {3,19,31942,8589,0,0,0,0,0},
      {3,20,19978,27197,0,0,0,0,0},
      {3,21,27060,15071,0,0,0,0,0},
      {3,22,6071,26649,0,0,0,0,0},
      {3,23,10393,11176,0,0,0,0,0},
      {3,24,9597,13370,0,0,0,0,0},
      {3,25,7081,17677,0,0,0,0,0},
      {3,26,1433,19513,0,0,0,0,0},
      {3,27,26925,9014,0,0,0,0,0},
      {3,28,19202,8900,0,0,0,0,0},
      {3,29,18152,30647,0,0,0,0,0},
      {3,30,20803,1737,0,0,0,0,0},
      {3,31,11804,25221,0,0,0,0,0},
      {3,32,31683,17783,0,0,0,0,0},
      {3,33,29694,9345,0,0,0,0,0},
      {3,34,12280,26611,0,0,0,0,0},
      {3,35,6526,26122,0,0,0,0,0},
      {3,36,26165,11241,0,0,0,0,0},
      {3,37,7666,26962,0,0,0,0,0},
      {3,38,16290,8480,0,0,0,0,0},
      {3,39,11774,10120,0,0,0,0,0},
      {3,40,30051,30426,0,0,0,0,0},
      {3,41,1335,15424,0,0,0,0,0},
      {3,42,6865,17742,0,0,0,0,0},
      {3,43,31779,12489,0,0,0,0,0},
      {3,44,32120,21001,0,0,0,0,0},
      {3,45,14508,6996,0,0,0,0,0},
      {3,46,979,25024,0,0,0,0,0},
      {3,47,4554,21896,0,0,0,0,0},
      {3,48,7989,21777,0,0,0,0,0},
      {3,49,4972,20661,0,0,0,0,0},
      {3,50,6612,2730,0,0,0,0,0},
      {3,51,12742,4418,0,0,0,0,0},
      {3,52,29194,595,0,0,0,0,0},
      {3,53,19267,20113,0,0,0,0,0}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_3_5N[108][13]=
    {
      {12,22422,10282,11626,19997,11161,2922,3122,99,5625,17064,8270,179},
      {12,25087,16218,17015,828,20041,25656,4186,11629,22599,17305,22515,6463},
      {12,11049,22853,25706,14388,5500,19245,8732,2177,13555,11346,17265,3069},
      {12,16581,22225,12563,19717,23577,11555,25496,6853,25403,5218,15925,21766},
      {12,16529,14487,7643,10715,17442,11119,5679,14155,24213,21000,1116,15620},
      {12,5340,8636,16693,1434,5635,6516,9482,20189,1066,15013,25361,14243},
      {12,18506,22236,20912,8952,5421,15691,6126,21595,500,6904,13059,6802},
      {12,8433,4694,5524,14216,3685,19721,25420,9937,23813,9047,25651,16826},
      {12,21500,24814,6344,17382,7064,13929,4004,16552,12818,8720,5286,2206},
      {12,22517,2429,19065,2921,21611,1873,7507,5661,23006,23128,20543,19777},
      {12,1770,4636,20900,14931,9247,12340,11008,12966,4471,2731,16445,791},
      {12,6635,14556,18865,22421,22124,12697,9803,25485,7744,18254,11313,9004},
      {12,19982,23963,18912,7206,12500,4382,20067,6177,21007,1195,23547,24837},
      {12,756,11158,14646,20534,3647,17728,11676,11843,12937,4402,8261,22944},
      {12,9306,24009,10012,11081,3746,24325,8060,19826,842,8836,2898,5019},
      {12,7575,7455,25244,4736,14400,22981,5543,8006,24203,13053,1120,5128},
      {12,3482,9270,13059,15825,7453,23747,3656,24585,16542,17507,22462,14670},
      {12,15627,15290,4198,22748,5842,13395,23918,16985,14929,3726,25350,24157},
      {12,24896,16365,16423,13461,16615,8107,24741,3604,25904,8716,9604,20365},
      {12,3729,17245,18448,9862,20831,25326,20517,24618,13282,5099,14183,8804},
      {12,16455,17646,15376,18194,25528,1777,6066,21855,14372,12517,4488,17490},
      {12,1400,8135,23375,20879,8476,4084,12936,25536,22309,16582,6402,24360},
      {12,25119,23586,128,4761,10443,22536,8607,9752,25446,15053,1856,4040},
      {12,377,21160,13474,5451,17170,5938,10256,11972,24210,17833,22047,16108},
      {12,13075,9648,24546,13150,23867,7309,19798,2988,16858,4825,23950,15125},
      {12,20526,3553,11525,23366,2452,17626,19265,20172,18060,24593,13255,1552},
      {12,18839,21132,20119,15214,14705,7096,10174,5663,18651,19700,12524,14033},
      {12,4127,2971,17499,16287,22368,21463,7943,18880,5567,8047,23363,6797},
      {12,10651,24471,14325,4081,7258,4949,7044,1078,797,22910,20474,4318},
      {12,21374,13231,22985,5056,3821,23718,14178,9978,19030,23594,8895,25358},
      {12,6199,22056,7749,13310,3999,23697,16445,22636,5225,22437,24153,9442},
      {12,7978,12177,2893,20778,3175,8645,11863,24623,10311,25767,17057,3691},
      {12,20473,11294,9914,22815,2574,8439,3699,5431,24840,21908,16088,18244},
      {12,8208,5755,19059,8541,24924,6454,11234,10492,16406,10831,11436,9649},
      {12,16264,11275,24953,2347,12667,19190,7257,7174,24819,2938,2522,11749},
      {12,3627,5969,13862,1538,23176,6353,2855,17720,2472,7428,573,15036},
      {3,0,18539,18661,0,0,0,0,0,0,0,0,0},
      {3,1,10502,3002,0,0,0,0,0,0,0,0,0},
      {3,2,9368,10761,0,0,0,0,0,0,0,0,0},
      {3,3,12299,7828,0,0,0,0,0,0,0,0,0},
      {3,4,15048,13362,0,0,0,0,0,0,0,0,0},
      {3,5,18444,24640,0,0,0,0,0,0,0,0,0},
      {3,6,20775,19175,0,0,0,0,0,0,0,0,0},
      {3,7,18970,10971,0,0,0,0,0,0,0,0,0},
      {3,8,5329,19982,0,0,0,0,0,0,0,0,0},
      {3,9,11296,18655,0,0,0,0,0,0,0,0,0},
      {3,10,15046,20659,0,0,0,0,0,0,0,0,0},
      {3,11,7300,22140,0,0,0,0,0,0,0,0,0},
      {3,12,22029,14477,0,0,0,0,0,0,0,0,0},
      {3,13,11129,742,0,0,0,0,0,0,0,0,0},
      {3,14,13254,13813,0,0,0,0,0,0,0,0,0},
      {3,15,19234,13273,0,0,0,0,0,0,0,0,0},
      {3,16,6079,21122,0,0,0,0,0,0,0,0,0},
      {3,17,22782,5828,0,0,0,0,0,0,0,0,0},
      {3,18,19775,4247,0,0,0,0,0,0,0,0,0},
      {3,19,1660,19413,0,0,0,0,0,0,0,0,0},
      {3,20,4403,3649,0,0,0,0,0,0,0,0,0},
      {3,21,13371,25851,0,0,0,0,0,0,0,0,0},
      {3,22,22770,21784,0,0,0,0,0,0,0,0,0},
      {3,23,10757,14131,0,0,0,0,0,0,0,0,0},
      {3,24,16071,21617,0,0,0,0,0,0,0,0,0},
      {3,25,6393,3725,0,0,0,0,0,0,0,0,0},
      {3,26,597,19968,0,0,0,0,0,0,0,0,0},
      {3,27,5743,8084,0,0,0,0,0,0,0,0,0},
      {3,28,6770,9548,0,0,0,0,0,0,0,0,0},
      {3,29,4285,17542,0,0,0,0,0,0,0,0,0},
      {3,30,13568,22599,0,0,0,0,0,0,0,0,0},
      {3,31,1786,4617,0,0,0,0,0,0,0,0,0},
      {3,32,23238,11648,0,0,0,0,0,0,0,0,0},
      {3,33,19627,2030,0,0,0,0,0,0,0,0,0},
      {3,34,13601,13458,0,0,0,0,0,0,0,0,0},
      {3,35,13740,17328,0,0,0,0,0,0,0,0,0},
      {3,36,25012,13944,0,0,0,0,0,0,0,0,0},
      {3,37,22513,6687,0,0,0,0,0,0,0,0,0},
      {3,38,4934,12587,0,0,0,0,0,0,0,0,0},
      {3,39,21197,5133,0,0,0,0,0,0,0,0,0},
      {3,40,22705,6938,0,0,0,0,0,0,0,0,0},
      {3,41,7534,24633,0,0,0,0,0,0,0,0,0},
      {3,42,24400,12797,0,0,0,0,0,0,0,0,0},
      {3,43,21911,25712,0,0,0,0,0,0,0,0,0},
      {3,44,12039,1140,0,0,0,0,0,0,0,0,0},
      {3,45,24306,1021,0,0,0,0,0,0,0,0,0},
      {3,46,14012,20747,0,0,0,0,0,0,0,0,0},
      {3,47,11265,15219,0,0,0,0,0,0,0,0,0},
      {3,48,4670,15531,0,0,0,0,0,0,0,0,0},
      {3,49,9417,14359,0,0,0,0,0,0,0,0,0},
      {3,50,2415,6504,0,0,0,0,0,0,0,0,0},
      {3,51,24964,24690,0,0,0,0,0,0,0,0,0},
      {3,52,14443,8816,0,0,0,0,0,0,0,0,0},
      {3,53,6926,1291,0,0,0,0,0,0,0,0,0},
      {3,54,6209,20806,0,0,0,0,0,0,0,0,0},
      {3,55,13915,4079,0,0,0,0,0,0,0,0,0},
      {3,56,24410,13196,0,0,0,0,0,0,0,0,0},
      {3,57,13505,6117,0,0,0,0,0,0,0,0,0},
      {3,58,9869,8220,0,0,0,0,0,0,0,0,0},
      {3,59,1570,6044,0,0,0,0,0,0,0,0,0},
      {3,60,25780,17387,0,0,0,0,0,0,0,0,0},
      {3,61,20671,24913,0,0,0,0,0,0,0,0,0},
      {3,62,24558,20591,0,0,0,0,0,0,0,0,0},
      {3,63,12402,3702,0,0,0,0,0,0,0,0,0},
      {3,64,8314,1357,0,0,0,0,0,0,0,0,0},
      {3,65,20071,14616,0,0,0,0,0,0,0,0,0},
      {3,66,17014,3688,0,0,0,0,0,0,0,0,0},
      {3,67,19837,946,0,0,0,0,0,0,0,0,0},
      {3,68,15195,12136,0,0,0,0,0,0,0,0,0},
      {3,69,7758,22808,0,0,0,0,0,0,0,0,0},
      {3,70,3564,2925,0,0,0,0,0,0,0,0,0},
      {3,71,3434,7769,0,0,0,0,0,0,0,0,0}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_2_3N_DVBT2[120][14]=
    {
      {13,317,2255,2324,2723,3538,3576,6194,6700,9101,10057,12739,17407,21039},
      {13,1958,2007,3294,4394,12762,14505,14593,14692,16522,17737,19245,21272,21379},
      {13,127,860,5001,5633,8644,9282,12690,14644,17553,19511,19681,20954,21002},
      {13,2514,2822,5781,6297,8063,9469,9551,11407,11837,12985,15710,20236,20393},
      {13,1565,3106,4659,4926,6495,6872,7343,8720,15785,16434,16727,19884,21325},
      {13,706,3220,8568,10896,12486,13663,16398,16599,19475,19781,20625,20961,21335},
      {13,4257,10449,12406,14561,16049,16522,17214,18029,18033,18802,19062,19526,20748},
      {13,412,433,558,2614,2978,4157,6584,9320,11683,11819,13024,14486,16860},
      {13,777,5906,7403,8550,8717,8770,11436,12846,13629,14755,15688,16392,16419},
      {13,4093,5045,6037,7248,8633,9771,10260,10809,11326,12072,17516,19344,19938},
      {13,2120,2648,3155,3852,6888,12258,14821,15359,16378,16437,17791,20614,21025},
      {13,1085,2434,5816,7151,8050,9422,10884,12728,15353,17733,18140,18729,20920},
      {3,856,1690,12787,0,0,0,0,0,0,0,0,0},
      {3,6532,7357,9151,0,0,0,0,0,0,0,0,0},
      {3,4210,16615,18152,0,0,0,0,0,0,0,0,0},
      {3,11494,14036,17470,0,0,0,0,0,0,0,0,0},
      {3,2474,10291,10323,0,0,0,0,0,0,0,0,0},
      {3,1778,6973,10739,0,0,0,0,0,0,0,0,0},
      {3,4347,9570,18748,0,0,0,0,0,0,0,0,0},
      {3,2189,11942,20666,0,0,0,0,0,0,0,0,0},
      {3,3868,7526,17706,0,0,0,0,0,0,0,0,0},
      {3,8780,14796,18268,0,0,0,0,0,0,0,0,0},
      {3,160,16232,17399,0,0,0,0,0,0,0,0,0},
      {3,1285,2003,18922,0,0,0,0,0,0,0,0,0},
      {3,4658,17331,20361,0,0,0,0,0,0,0,0,0},
      {3,2765,4862,5875,0,0,0,0,0,0,0,0,0},
      {3,4565,5521,8759,0,0,0,0,0,0,0,0,0},
      {3,3484,7305,15829,0,0,0,0,0,0,0,0,0},
      {3,5024,17730,17879,0,0,0,0,0,0,0,0,0},
      {3,7031,12346,15024,0,0,0,0,0,0,0,0,0},
      {3,179,6365,11352,0,0,0,0,0,0,0,0,0},
      {3,2490,3143,5098,0,0,0,0,0,0,0,0,0},
      {3,2643,3101,21259,0,0,0,0,0,0,0,0,0},
      {3,4315,4724,13130,0,0,0,0,0,0,0,0,0},
      {3,594,17365,18322,0,0,0,0,0,0,0,0,0},
      {3,5983,8597,9627,0,0,0,0,0,0,0,0,0},
      {3,10837,15102,20876,0,0,0,0,0,0,0,0,0},
      {3,10448,20418,21478,0,0,0,0,0,0,0,0,0},
      {3,3848,12029,15228,0,0,0,0,0,0,0,0,0},
      {3,708,5652,13146,0,0,0,0,0,0,0,0,0},
      {3,5998,7534,16117,0,0,0,0,0,0,0,0,0},
      {3,2098,13201,18317,0,0,0,0,0,0,0,0,0},
      {3,9186,14548,17776,0,0,0,0,0,0,0,0,0},
      {3,5246,10398,18597,0,0,0,0,0,0,0,0,0},
      {3,3083,4944,21021,0,0,0,0,0,0,0,0,0},
      {3,13726,18495,19921,0,0,0,0,0,0,0,0,0},
      {3,6736,10811,17545,0,0,0,0,0,0,0,0,0},
      {3,10084,12411,14432,0,0,0,0,0,0,0,0,0},
      {3,1064,13555,17033,0,0,0,0,0,0,0,0,0},
      {3,679,9878,13547,0,0,0,0,0,0,0,0,0},
      {3,3422,9910,20194,0,0,0,0,0,0,0,0,0},
      {3,3640,3701,10046,0,0,0,0,0,0,0,0,0},
      {3,5862,10134,11498,0,0,0,0,0,0,0,0,0},
      {3,5923,9580,15060,0,0,0,0,0,0,0,0,0},
      {3,1073,3012,16427,0,0,0,0,0,0,0,0,0},
      {3,5527,20113,20883,0,0,0,0,0,0,0,0,0},
      {3,7058,12924,15151,0,0,0,0,0,0,0,0,0},
      {3,9764,12230,17375,0,0,0,0,0,0,0,0,0},
      {3,772,7711,12723,0,0,0,0,0,0,0,0,0},
      {3,555,13816,15376,0,0,0,0,0,0,0,0,0},
      {3,10574,11268,17932,0,0,0,0,0,0,0,0,0},
      {3,15442,17266,20482,0,0,0,0,0,0,0,0,0},
      {3,390,3371,8781,0,0,0,0,0,0,0,0,0},
      {3,10512,12216,17180,0,0,0,0,0,0,0,0,0},
      {3,4309,14068,15783,0,0,0,0,0,0,0,0,0},
      {3,3971,11673,20009,0,0,0,0,0,0,0,0,0},
      {3,9259,14270,17199,0,0,0,0,0,0,0,0,0},
      {3,2947,5852,20101,0,0,0,0,0,0,0,0,0},
      {3,3965,9722,15363,0,0,0,0,0,0,0,0,0},
      {3,1429,5689,16771,0,0,0,0,0,0,0,0,0},
      {3,6101,6849,12781,0,0,0,0,0,0,0,0,0},
      {3,3676,9347,18761,0,0,0,0,0,0,0,0,0},
      {3,350,11659,18342,0,0,0,0,0,0,0,0,0},
      {3,5961,14803,16123,0,0,0,0,0,0,0,0,0},
      {3,2113,9163,13443,0,0,0,0,0,0,0,0,0},
      {3,2155,9808,12885,0,0,0,0,0,0,0,0,0},
      {3,2861,7988,11031,0,0,0,0,0,0,0,0,0},
      {3,7309,9220,20745,0,0,0,0,0,0,0,0,0},
      {3,6834,8742,11977,0,0,0,0,0,0,0,0,0},
      {3,2133,12908,14704,0,0,0,0,0,0,0,0,0},
      {3,10170,13809,18153,0,0,0,0,0,0,0,0,0},
      {3,13464,14787,14975,0,0,0,0,0,0,0,0,0},
      {3,799,1107,3789,0,0,0,0,0,0,0,0,0},
      {3,3571,8176,10165,0,0,0,0,0,0,0,0,0},
      {3,5433,13446,15481,0,0,0,0,0,0,0,0,0},
      {3,3351,6767,12840,0,0,0,0,0,0,0,0,0},
      {3,8950,8974,11650,0,0,0,0,0,0,0,0,0},
      {3,1430,4250,21332,0,0,0,0,0,0,0,0,0},
      {3,6283,10628,15050,0,0,0,0,0,0,0,0,0},
      {3,8632,14404,16916,0,0,0,0,0,0,0,0,0},
      {3,6509,10702,16278,0,0,0,0,0,0,0,0,0},
      {3,15900,16395,17995,0,0,0,0,0,0,0,0,0},
      {3,8031,18420,19733,0,0,0,0,0,0,0,0,0},
      {3,3747,4634,17087,0,0,0,0,0,0,0,0,0},
      {3,4453,6297,16262,0,0,0,0,0,0,0,0,0},
      {3,2792,3513,17031,0,0,0,0,0,0,0,0,0},
      {3,14846,20893,21563,0,0,0,0,0,0,0,0,0},
      {3,17220,20436,21337,0,0,0,0,0,0,0,0,0},
      {3,275,4107,10497,0,0,0,0,0,0,0,0,0},
      {3,3536,7520,10027,0,0,0,0,0,0,0,0,0},
      {3,14089,14943,19455,0,0,0,0,0,0,0,0,0},
      {3,1965,3931,21104,0,0,0,0,0,0,0,0,0},
      {3,2439,11565,17932,0,0,0,0,0,0,0,0,0},
      {3,154,15279,21414,0,0,0,0,0,0,0,0,0},
      {3,10017,11269,16546,0,0,0,0,0,0,0,0,0},
      {3,7169,10161,16928,0,0,0,0,0,0,0,0,0},
      {3,10284,16791,20655,0,0,0,0,0,0,0,0,0},
      {3,36,3175,8475,0,0,0,0,0,0,0,0,0},
      {3,2605,16269,19290,0,0,0,0,0,0,0,0,0},
      {3,8947,9178,15420,0,0,0,0,0,0,0,0,0},
      {3,5687,9156,12408,0,0,0,0,0,0,0,0,0},
      {3,8096,9738,14711,0,0,0,0,0,0,0,0,0},
      {3,4935,8093,19266,0,0,0,0,0,0,0,0,0},
      {3,2667,10062,15972,0,0,0,0,0,0,0,0,0},
      {3,6389,11318,14417,0,0,0,0,0,0,0,0,0},
      {3,8800,18137,18434,0,0,0,0,0,0,0,0,0},
      {3,5824,5927,15314,0,0,0,0,0,0,0,0,0},
      {3,6056,13168,15179,0,0,0,0,0,0,0,0,0},
      {3,3284,13138,18919,0,0,0,0,0,0,0,0,0},
      {3,13115,17259,17332,0,0,0,0,0,0,0,0,0}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_3_4N[135][13]=
    {
      {12,0,6385,7901,14611,13389,11200,3252,5243,2504,2722,821,7374},
      {12,1,11359,2698,357,13824,12772,7244,6752,15310,852,2001,11417},
      {12,2,7862,7977,6321,13612,12197,14449,15137,13860,1708,6399,13444},
      {12,3,1560,11804,6975,13292,3646,3812,8772,7306,5795,14327,7866},
      {12,4,7626,11407,14599,9689,1628,2113,10809,9283,1230,15241,4870},
      {12,5,1610,5699,15876,9446,12515,1400,6303,5411,14181,13925,7358},
      {12,6,4059,8836,3405,7853,7992,15336,5970,10368,10278,9675,4651},
      {12,7,4441,3963,9153,2109,12683,7459,12030,12221,629,15212,406},
      {12,8,6007,8411,5771,3497,543,14202,875,9186,6235,13908,3563},
      {12,9,3232,6625,4795,546,9781,2071,7312,3399,7250,4932,12652},
      {12,10,8820,10088,11090,7069,6585,13134,10158,7183,488,7455,9238},
      {12,11,1903,10818,119,215,7558,11046,10615,11545,14784,7961,15619},
      {12,12,3655,8736,4917,15874,5129,2134,15944,14768,7150,2692,1469},
      {12,13,8316,3820,505,8923,6757,806,7957,4216,15589,13244,2622},
      {12,14,14463,4852,15733,3041,11193,12860,13673,8152,6551,15108,8758},
      {3,15,3149,11981,0,0,0,0,0,0,0,0,0},
      {3,16,13416,6906,0,0,0,0,0,0,0,0,0},
      {3,17,13098,13352,0,0,0,0,0,0,0,0,0},
      {3,18,2009,14460,0,0,0,0,0,0,0,0,0},
      {3,19,7207,4314,0,0,0,0,0,0,0,0,0},
      {3,20,3312,3945,0,0,0,0,0,0,0,0,0},
      {3,21,4418,6248,0,0,0,0,0,0,0,0,0},
      {3,22,2669,13975,0,0,0,0,0,0,0,0,0},
      {3,23,7571,9023,0,0,0,0,0,0,0,0,0},
      {3,24,14172,2967,0,0,0,0,0,0,0,0,0},
      {3,25,7271,7138,0,0,0,0,0,0,0,0,0},
      {3,26,6135,13670,0,0,0,0,0,0,0,0,0},
      {3,27,7490,14559,0,0,0,0,0,0,0,0,0},
      {3,28,8657,2466,0,0,0,0,0,0,0,0,0},
      {3,29,8599,12834,0,0,0,0,0,0,0,0,0},
      {3,30,3470,3152,0,0,0,0,0,0,0,0,0},
      {3,31,13917,4365,0,0,0,0,0,0,0,0,0},
      {3,32,6024,13730,0,0,0,0,0,0,0,0,0},
      {3,33,10973,14182,0,0,0,0,0,0,0,0,0},
      {3,34,2464,13167,0,0,0,0,0,0,0,0,0},
      {3,35,5281,15049,0,0,0,0,0,0,0,0,0},
      {3,36,1103,1849,0,0,0,0,0,0,0,0,0},
      {3,37,2058,1069,0,0,0,0,0,0,0,0,0},
      {3,38,9654,6095,0,0,0,0,0,0,0,0,0},
      {3,39,14311,7667,0,0,0,0,0,0,0,0,0},
      {3,40,15617,8146,0,0,0,0,0,0,0,0,0},
      {3,41,4588,11218,0,0,0,0,0,0,0,0,0},
      {3,42,13660,6243,0,0,0,0,0,0,0,0,0},
      {3,43,8578,7874,0,0,0,0,0,0,0,0,0},
      {3,44,11741,2686,0,0,0,0,0,0,0,0,0},
      {3,0,1022,1264,0,0,0,0,0,0,0,0,0},
      {3,1,12604,9965,0,0,0,0,0,0,0,0,0},
      {3,2,8217,2707,0,0,0,0,0,0,0,0,0},
      {3,3,3156,11793,0,0,0,0,0,0,0,0,0},
      {3,4,354,1514,0,0,0,0,0,0,0,0,0},
      {3,5,6978,14058,0,0,0,0,0,0,0,0,0},
      {3,6,7922,16079,0,0,0,0,0,0,0,0,0},
      {3,7,15087,12138,0,0,0,0,0,0,0,0,0},
      {3,8,5053,6470,0,0,0,0,0,0,0,0,0},
      {3,9,12687,14932,0,0,0,0,0,0,0,0,0},
      {3,10,15458,1763,0,0,0,0,0,0,0,0,0},
      {3,11,8121,1721,0,0,0,0,0,0,0,0,0},
      {3,12,12431,549,0,0,0,0,0,0,0,0,0},
      {3,13,4129,7091,0,0,0,0,0,0,0,0,0},
      {3,14,1426,8415,0,0,0,0,0,0,0,0,0},
      {3,15,9783,7604,0,0,0,0,0,0,0,0,0},
      {3,16,6295,11329,0,0,0,0,0,0,0,0,0},
      {3,17,1409,12061,0,0,0,0,0,0,0,0,0},
      {3,18,8065,9087,0,0,0,0,0,0,0,0,0},
      {3,19,2918,8438,0,0,0,0,0,0,0,0,0},
      {3,20,1293,14115,0,0,0,0,0,0,0,0,0},
      {3,21,3922,13851,0,0,0,0,0,0,0,0,0},
      {3,22,3851,4000,0,0,0,0,0,0,0,0,0},
      {3,23,5865,1768,0,0,0,0,0,0,0,0,0},
      {3,24,2655,14957,0,0,0,0,0,0,0,0,0},
      {3,25,5565,6332,0,0,0,0,0,0,0,0,0},
      {3,26,4303,12631,0,0,0,0,0,0,0,0,0},
      {3,27,11653,12236,0,0,0,0,0,0,0,0,0},
      {3,28,16025,7632,0,0,0,0,0,0,0,0,0},
      {3,29,4655,14128,0,0,0,0,0,0,0,0,0},
      {3,30,9584,13123,0,0,0,0,0,0,0,0,0},
      {3,31,13987,9597,0,0,0,0,0,0,0,0,0},
      {3,32,15409,12110,0,0,0,0,0,0,0,0,0},
      {3,33,8754,15490,0,0,0,0,0,0,0,0,0},
      {3,34,7416,15325,0,0,0,0,0,0,0,0,0},
      {3,35,2909,15549,0,0,0,0,0,0,0,0,0},
      {3,36,2995,8257,0,0,0,0,0,0,0,0,0},
      {3,37,9406,4791,0,0,0,0,0,0,0,0,0},
      {3,38,11111,4854,0,0,0,0,0,0,0,0,0},
      {3,39,2812,8521,0,0,0,0,0,0,0,0,0},
      {3,40,8476,14717,0,0,0,0,0,0,0,0,0},
      {3,41,7820,15360,0,0,0,0,0,0,0,0,0},
      {3,42,1179,7939,0,0,0,0,0,0,0,0,0},
      {3,43,2357,8678,0,0,0,0,0,0,0,0,0},
      {3,44,7703,6216,0,0,0,0,0,0,0,0,0},
      {3,0,3477,7067,0,0,0,0,0,0,0,0,0},
      {3,1,3931,13845,0,0,0,0,0,0,0,0,0},
      {3,2,7675,12899,0,0,0,0,0,0,0,0,0},
      {3,3,1754,8187,0,0,0,0,0,0,0,0,0},
      {3,4,7785,1400,0,0,0,0,0,0,0,0,0},
      {3,5,9213,5891,0,0,0,0,0,0,0,0,0},
      {3,6,2494,7703,0,0,0,0,0,0,0,0,0},
      {3,7,2576,7902,0,0,0,0,0,0,0,0,0},
      {3,8,4821,15682,0,0,0,0,0,0,0,0,0},
      {3,9,10426,11935,0,0,0,0,0,0,0,0,0},
      {3,10,1810,904,0,0,0,0,0,0,0,0,0},
      {3,11,11332,9264,0,0,0,0,0,0,0,0,0},
      {3,12,11312,3570,0,0,0,0,0,0,0,0,0},
      {3,13,14916,2650,0,0,0,0,0,0,0,0,0},
      {3,14,7679,7842,0,0,0,0,0,0,0,0,0},
      {3,15,6089,13084,0,0,0,0,0,0,0,0,0},
      {3,16,3938,2751,0,0,0,0,0,0,0,0,0},
      {3,17,8509,4648,0,0,0,0,0,0,0,0,0},
      {3,18,12204,8917,0,0,0,0,0,0,0,0,0},
      {3,19,5749,12443,0,0,0,0,0,0,0,0,0},
      {3,20,12613,4431,0,0,0,0,0,0,0,0,0},
      {3,21,1344,4014,0,0,0,0,0,0,0,0,0},
      {3,22,8488,13850,0,0,0,0,0,0,0,0,0},
      {3,23,1730,14896,0,0,0,0,0,0,0,0,0},
      {3,24,14942,7126,0,0,0,0,0,0,0,0,0},
      {3,25,14983,8863,0,0,0,0,0,0,0,0,0},
      {3,26,6578,8564,0,0,0,0,0,0,0,0,0},
      {3,27,4947,396,0,0,0,0,0,0,0,0,0},
      {3,28,297,12805,0,0,0,0,0,0,0,0,0},
      {3,29,13878,6692,0,0,0,0,0,0,0,0,0},
      {3,30,11857,11186,0,0,0,0,0,0,0,0,0},
      {3,31,14395,11493,0,0,0,0,0,0,0,0,0},
      {3,32,16145,12251,0,0,0,0,0,0,0,0,0},
      {3,33,13462,7428,0,0,0,0,0,0,0,0,0},
      {3,34,14526,13119,0,0,0,0,0,0,0,0,0},
      {3,35,2535,11243,0,0,0,0,0,0,0,0,0},
      {3,36,6465,12690,0,0,0,0,0,0,0,0,0},
      {3,37,6872,9334,0,0,0,0,0,0,0,0,0},
      {3,38,15371,14023,0,0,0,0,0,0,0,0,0},
      {3,39,8101,10187,0,0,0,0,0,0,0,0,0},
      {3,40,11963,4848,0,0,0,0,0,0,0,0,0},
      {3,41,15125,6119,0,0,0,0,0,0,0,0,0},
      {3,42,8051,14465,0,0,0,0,0,0,0,0,0},
      {3,43,11139,5167,0,0,0,0,0,0,0,0,0},
      {3,44,2883,14521,0,0,0,0,0,0,0,0,0}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_4_5N[144][12]=
    {
      {11,0,149,11212,5575,6360,12559,8108,8505,408,10026,12828},
      {11,1,5237,490,10677,4998,3869,3734,3092,3509,7703,10305},
      {11,2,8742,5553,2820,7085,12116,10485,564,7795,2972,2157},
      {11,3,2699,4304,8350,712,2841,3250,4731,10105,517,7516},
      {11,4,12067,1351,11992,12191,11267,5161,537,6166,4246,2363},
      {11,5,6828,7107,2127,3724,5743,11040,10756,4073,1011,3422},
      {11,6,11259,1216,9526,1466,10816,940,3744,2815,11506,11573},
      {11,7,4549,11507,1118,1274,11751,5207,7854,12803,4047,6484},
      {11,8,8430,4115,9440,413,4455,2262,7915,12402,8579,7052},
      {11,9,3885,9126,5665,4505,2343,253,4707,3742,4166,1556},
      {11,10,1704,8936,6775,8639,8179,7954,8234,7850,8883,8713},
      {11,11,11716,4344,9087,11264,2274,8832,9147,11930,6054,5455},
      {11,12,7323,3970,10329,2170,8262,3854,2087,12899,9497,11700},
      {11,13,4418,1467,2490,5841,817,11453,533,11217,11962,5251},
      {11,14,1541,4525,7976,3457,9536,7725,3788,2982,6307,5997},
      {11,15,11484,2739,4023,12107,6516,551,2572,6628,8150,9852},
      {11,16,6070,1761,4627,6534,7913,3730,11866,1813,12306,8249},
      {11,17,12441,5489,8748,7837,7660,2102,11341,2936,6712,11977},
      {3,18,10155,4210,0,0,0,0,0,0,0,0},
      {3,19,1010,10483,0,0,0,0,0,0,0,0},
      {3,20,8900,10250,0,0,0,0,0,0,0,0},
      {3,21,10243,12278,0,0,0,0,0,0,0,0},
      {3,22,7070,4397,0,0,0,0,0,0,0,0},
      {3,23,12271,3887,0,0,0,0,0,0,0,0},
      {3,24,11980,6836,0,0,0,0,0,0,0,0},
      {3,25,9514,4356,0,0,0,0,0,0,0,0},
      {3,26,7137,10281,0,0,0,0,0,0,0,0},
      {3,27,11881,2526,0,0,0,0,0,0,0,0},
      {3,28,1969,11477,0,0,0,0,0,0,0,0},
      {3,29,3044,10921,0,0,0,0,0,0,0,0},
      {3,30,2236,8724,0,0,0,0,0,0,0,0},
      {3,31,9104,6340,0,0,0,0,0,0,0,0},
      {3,32,7342,8582,0,0,0,0,0,0,0,0},
      {3,33,11675,10405,0,0,0,0,0,0,0,0},
      {3,34,6467,12775,0,0,0,0,0,0,0,0},
      {3,35,3186,12198,0,0,0,0,0,0,0,0},
      {3,0,9621,11445,0,0,0,0,0,0,0,0},
      {3,1,7486,5611,0,0,0,0,0,0,0,0},
      {3,2,4319,4879,0,0,0,0,0,0,0,0},
      {3,3,2196,344,0,0,0,0,0,0,0,0},
      {3,4,7527,6650,0,0,0,0,0,0,0,0},
      {3,5,10693,2440,0,0,0,0,0,0,0,0},
      {3,6,6755,2706,0,0,0,0,0,0,0,0},
      {3,7,5144,5998,0,0,0,0,0,0,0,0},
      {3,8,11043,8033,0,0,0,0,0,0,0,0},
      {3,9,4846,4435,0,0,0,0,0,0,0,0},
      {3,10,4157,9228,0,0,0,0,0,0,0,0},
      {3,11,12270,6562,0,0,0,0,0,0,0,0},
      {3,12,11954,7592,0,0,0,0,0,0,0,0},
      {3,13,7420,2592,0,0,0,0,0,0,0,0},
      {3,14,8810,9636,0,0,0,0,0,0,0,0},
      {3,15,689,5430,0,0,0,0,0,0,0,0},
      {3,16,920,1304,0,0,0,0,0,0,0,0},
      {3,17,1253,11934,0,0,0,0,0,0,0,0},
      {3,18,9559,6016,0,0,0,0,0,0,0,0},
      {3,19,312,7589,0,0,0,0,0,0,0,0},
      {3,20,4439,4197,0,0,0,0,0,0,0,0},
      {3,21,4002,9555,0,0,0,0,0,0,0,0},
      {3,22,12232,7779,0,0,0,0,0,0,0,0},
      {3,23,1494,8782,0,0,0,0,0,0,0,0},
      {3,24,10749,3969,0,0,0,0,0,0,0,0},
      {3,25,4368,3479,0,0,0,0,0,0,0,0},
      {3,26,6316,5342,0,0,0,0,0,0,0,0},
      {3,27,2455,3493,0,0,0,0,0,0,0,0},
      {3,28,12157,7405,0,0,0,0,0,0,0,0},
      {3,29,6598,11495,0,0,0,0,0,0,0,0},
      {3,30,11805,4455,0,0,0,0,0,0,0,0},
      {3,31,9625,2090,0,0,0,0,0,0,0,0},
      {3,32,4731,2321,0,0,0,0,0,0,0,0},
      {3,33,3578,2608,0,0,0,0,0,0,0,0},
      {3,34,8504,1849,0,0,0,0,0,0,0,0},
      {3,35,4027,1151,0,0,0,0,0,0,0,0},
      {3,0,5647,4935,0,0,0,0,0,0,0,0},
      {3,1,4219,1870,0,0,0,0,0,0,0,0},
      {3,2,10968,8054,0,0,0,0,0,0,0,0},
      {3,3,6970,5447,0,0,0,0,0,0,0,0},
      {3,4,3217,5638,0,0,0,0,0,0,0,0},
      {3,5,8972,669,0,0,0,0,0,0,0,0},
      {3,6,5618,12472,0,0,0,0,0,0,0,0},
      {3,7,1457,1280,0,0,0,0,0,0,0,0},
      {3,8,8868,3883,0,0,0,0,0,0,0,0},
      {3,9,8866,1224,0,0,0,0,0,0,0,0},
      {3,10,8371,5972,0,0,0,0,0,0,0,0},
      {3,11,266,4405,0,0,0,0,0,0,0,0},
      {3,12,3706,3244,0,0,0,0,0,0,0,0},
      {3,13,6039,5844,0,0,0,0,0,0,0,0},
      {3,14,7200,3283,0,0,0,0,0,0,0,0},
      {3,15,1502,11282,0,0,0,0,0,0,0,0},
      {3,16,12318,2202,0,0,0,0,0,0,0,0},
      {3,17,4523,965,0,0,0,0,0,0,0,0},
      {3,18,9587,7011,0,0,0,0,0,0,0,0},
      {3,19,2552,2051,0,0,0,0,0,0,0,0},
      {3,20,12045,10306,0,0,0,0,0,0,0,0},
      {3,21,11070,5104,0,0,0,0,0,0,0,0},
      {3,22,6627,6906,0,0,0,0,0,0,0,0},
      {3,23,9889,2121,0,0,0,0,0,0,0,0},
      {3,24,829,9701,0,0,0,0,0,0,0,0},
      {3,25,2201,1819,0,0,0,0,0,0,0,0},
      {3,26,6689,12925,0,0,0,0,0,0,0,0},
      {3,27,2139,8757,0,0,0,0,0,0,0,0},
      {3,28,12004,5948,0,0,0,0,0,0,0,0},
      {3,29,8704,3191,0,0,0,0,0,0,0,0},
      {3,30,8171,10933,0,0,0,0,0,0,0,0},
      {3,31,6297,7116,0,0,0,0,0,0,0,0},
      {3,32,616,7146,0,0,0,0,0,0,0,0},
      {3,33,5142,9761,0,0,0,0,0,0,0,0},
      {3,34,10377,8138,0,0,0,0,0,0,0,0},
      {3,35,7616,5811,0,0,0,0,0,0,0,0},
      {3,0,7285,9863,0,0,0,0,0,0,0,0},
      {3,1,7764,10867,0,0,0,0,0,0,0,0},
      {3,2,12343,9019,0,0,0,0,0,0,0,0},
      {3,3,4414,8331,0,0,0,0,0,0,0,0},
      {3,4,3464,642,0,0,0,0,0,0,0,0},
      {3,5,6960,2039,0,0,0,0,0,0,0,0},
      {3,6,786,3021,0,0,0,0,0,0,0,0},
      {3,7,710,2086,0,0,0,0,0,0,0,0},
      {3,8,7423,5601,0,0,0,0,0,0,0,0},
      {3,9,8120,4885,0,0,0,0,0,0,0,0},
      {3,10,12385,11990,0,0,0,0,0,0,0,0},
      {3,11,9739,10034,0,0,0,0,0,0,0,0},
      {3,12,424,10162,0,0,0,0,0,0,0,0},
      {3,13,1347,7597,0,0,0,0,0,0,0,0},
      {3,14,1450,112,0,0,0,0,0,0,0,0},
      {3,15,7965,8478,0,0,0,0,0,0,0,0},
      {3,16,8945,7397,0,0,0,0,0,0,0,0},
      {3,17,6590,8316,0,0,0,0,0,0,0,0},
      {3,18,6838,9011,0,0,0,0,0,0,0,0},
      {3,19,6174,9410,0,0,0,0,0,0,0,0},
      {3,20,255,113,0,0,0,0,0,0,0,0},
      {3,21,6197,5835,0,0,0,0,0,0,0,0},
      {3,22,12902,3844,0,0,0,0,0,0,0,0},
      {3,23,4377,3505,0,0,0,0,0,0,0,0},
      {3,24,5478,8672,0,0,0,0,0,0,0,0},
      {3,25,4453,2132,0,0,0,0,0,0,0,0},
      {3,26,9724,1380,0,0,0,0,0,0,0,0},
      {3,27,12131,11526,0,0,0,0,0,0,0,0},
      {3,28,12323,9511,0,0,0,0,0,0,0,0},
      {3,29,8231,1752,0,0,0,0,0,0,0,0},
      {3,30,497,9022,0,0,0,0,0,0,0,0},
      {3,31,9288,3080,0,0,0,0,0,0,0,0},
      {3,32,2481,7515,0,0,0,0,0,0,0,0},
      {3,33,2696,268,0,0,0,0,0,0,0,0},
      {3,34,4023,12341,0,0,0,0,0,0,0,0},
      {3,35,7108,5553,0,0,0,0,0,0,0,0}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_5_6N[150][14]=
    {
      {13,0,4362,416,8909,4156,3216,3112,2560,2912,6405,8593,4969,6723},
      {13,1,2479,1786,8978,3011,4339,9313,6397,2957,7288,5484,6031,10217},
      {13,2,10175,9009,9889,3091,4985,7267,4092,8874,5671,2777,2189,8716},
      {13,3,9052,4795,3924,3370,10058,1128,9996,10165,9360,4297,434,5138},
      {13,4,2379,7834,4835,2327,9843,804,329,8353,7167,3070,1528,7311},
      {13,5,3435,7871,348,3693,1876,6585,10340,7144,5870,2084,4052,2780},
      {13,6,3917,3111,3476,1304,10331,5939,5199,1611,1991,699,8316,9960},
      {13,7,6883,3237,1717,10752,7891,9764,4745,3888,10009,4176,4614,1567},
      {13,8,10587,2195,1689,2968,5420,2580,2883,6496,111,6023,1024,4449},
      {13,9,3786,8593,2074,3321,5057,1450,3840,5444,6572,3094,9892,1512},
      {13,10,8548,1848,10372,4585,7313,6536,6379,1766,9462,2456,5606,9975},
      {13,11,8204,10593,7935,3636,3882,394,5968,8561,2395,7289,9267,9978},
      {13,12,7795,74,1633,9542,6867,7352,6417,7568,10623,725,2531,9115},
      {13,13,7151,2482,4260,5003,10105,7419,9203,6691,8798,2092,8263,3755},
      {13,14,3600,570,4527,200,9718,6771,1995,8902,5446,768,1103,6520},
      {3,15,6304,7621,0,0,0,0,0,0,0,0,0,0},
      {3,16,6498,9209,0,0,0,0,0,0,0,0,0,0},
      {3,17,7293,6786,0,0,0,0,0,0,0,0,0,0},
      {3,18,5950,1708,0,0,0,0,0,0,0,0,0,0},
      {3,19,8521,1793,0,0,0,0,0,0,0,0,0,0},
      {3,20,6174,7854,0,0,0,0,0,0,0,0,0,0},
      {3,21,9773,1190,0,0,0,0,0,0,0,0,0,0},
      {3,22,9517,10268,0,0,0,0,0,0,0,0,0,0},
      {3,23,2181,9349,0,0,0,0,0,0,0,0,0,0},
      {3,24,1949,5560,0,0,0,0,0,0,0,0,0,0},
      {3,25,1556,555,0,0,0,0,0,0,0,0,0,0},
      {3,26,8600,3827,0,0,0,0,0,0,0,0,0,0},
      {3,27,5072,1057,0,0,0,0,0,0,0,0,0,0},
      {3,28,7928,3542,0,0,0,0,0,0,0,0,0,0},
      {3,29,3226,3762,0,0,0,0,0,0,0,0,0,0},
      {3,0,7045,2420,0,0,0,0,0,0,0,0,0,0},
      {3,1,9645,2641,0,0,0,0,0,0,0,0,0},
      {3,2,2774,2452,0,0,0,0,0,0,0,0,0,0},
      {3,3,5331,2031,0,0,0,0,0,0,0,0,0,0},
      {3,4,9400,7503,0,0,0,0,0,0,0,0,0,0},
      {3,5,1850,2338,0,0,0,0,0,0,0,0,0,0},
      {3,6,10456,9774,0,0,0,0,0,0,0,0,0,0},
      {3,7,1692,9276,0,0,0,0,0,0,0,0,0,0},
      {3,8,10037,4038,0,0,0,0,0,0,0,0,0,0},
      {3,9,3964,338,0,0,0,0,0,0,0,0,0,0},
      {3,10,2640,5087,0,0,0,0,0,0,0,0,0,0},
      {3,11,858,3473,0,0,0,0,0,0,0,0,0,0},
      {3,12,5582,5683,0,0,0,0,0,0,0,0,0,0},
      {3,13,9523,916,0,0,0,0,0,0,0,0,0,0},
      {3,14,4107,1559,0,0,0,0,0,0,0,0,0,0},
      {3,15,4506,3491,0,0,0,0,0,0,0,0,0,0},
      {3,16,8191,4182,0,0,0,0,0,0,0,0,0,0},
      {3,17,10192,6157,0,0,0,0,0,0,0,0,0,0},
      {3,18,5668,3305,0,0,0,0,0,0,0,0,0,0},
      {3,19,3449,1540,0,0,0,0,0,0,0,0,0,0},
      {3,20,4766,2697,0,0,0,0,0,0,0,0,0,0},
      {3,21,4069,6675,0,0,0,0,0,0,0,0,0,0},
      {3,22,1117,1016,0,0,0,0,0,0,0,0,0,0},
      {3,23,5619,3085,0,0,0,0,0,0,0,0,0,0},
      {3,24,8483,8400,0,0,0,0,0,0,0,0,0,0},
      {3,25,8255,394,0,0,0,0,0,0,0,0,0,0},
      {3,26,6338,5042,0,0,0,0,0,0,0,0,0,0},
      {3,27,6174,5119,0,0,0,0,0,0,0,0,0,0},
      {3,28,7203,1989,0,0,0,0,0,0,0,0,0,0},
      {3,29,1781,5174,0,0,0,0,0,0,0,0,0,0},
      {3,0,1464,3559,0,0,0,0,0,0,0,0,0,0},
      {3,1,3376,4214,0,0,0,0,0,0,0,0,0,0},
      {3,2,7238,67,0,0,0,0,0,0,0,0,0,0},
      {3,3,10595,8831,0,0,0,0,0,0,0,0,0,0},
      {3,4,1221,6513,0,0,0,0,0,0,0,0,0,0},
      {3,5,5300,4652,0,0,0,0,0,0,0,0,0,0},
      {3,6,1429,9749,0,0,0,0,0,0,0,0,0,0},
      {3,7,7878,5131,0,0,0,0,0,0,0,0,0,0},
      {3,8,4435,10284,0,0,0,0,0,0,0,0,0,0},
      {3,9,6331,5507,0,0,0,0,0,0,0,0,0,0},
      {3,10,6662,4941,0,0,0,0,0,0,0,0,0,0},
      {3,11,9614,10238,0,0,0,0,0,0,0,0,0,0},
      {3,12,8400,8025,0,0,0,0,0,0,0,0,0,0},
      {3,13,9156,5630,0,0,0,0,0,0,0,0,0,0},
      {3,14,7067,8878,0,0,0,0,0,0,0,0,0,0},
      {3,15,9027,3415,0,0,0,0,0,0,0,0,0,0},
      {3,16,1690,3866,0,0,0,0,0,0,0,0,0,0},
      {3,17,2854,8469,0,0,0,0,0,0,0,0,0,0},
      {3,18,6206,630,0,0,0,0,0,0,0,0,0,0},
      {3,19,363,5453,0,0,0,0,0,0,0,0,0,0},
      {3,20,4125,7008,0,0,0,0,0,0,0,0,0,0},
      {3,21,1612,6702,0,0,0,0,0,0,0,0,0,0},
      {3,22,9069,9226,0,0,0,0,0,0,0,0,0,0},
      {3,23,5767,4060,0,0,0,0,0,0,0,0,0,0},
      {3,24,3743,9237,0,0,0,0,0,0,0,0,0,0},
      {3,25,7018,5572,0,0,0,0,0,0,0,0,0,0},
      {3,26,8892,4536,0,0,0,0,0,0,0,0,0,0},
      {3,27,853,6064,0,0,0,0,0,0,0,0,0,0},
      {3,28,8069,5893,0,0,0,0,0,0,0,0,0,0},
      {3,29,2051,2885,0,0,0,0,0,0,0,0,0,0},
      {3,0,10691,3153,0,0,0,0,0,0,0,0,0,0},
      {3,1,3602,4055,0,0,0,0,0,0,0,0,0,0},
      {3,2,328,1717,0,0,0,0,0,0,0,0,0,0},
      {3,3,2219,9299,0,0,0,0,0,0,0,0,0,0},
      {3,4,1939,7898,0,0,0,0,0,0,0,0,0,0},
      {3,5,617,206,0,0,0,0,0,0,0,0,0,0},
      {3,6,8544,1374,0,0,0,0,0,0,0,0,0,0},
      {3,7,10676,3240,0,0,0,0,0,0,0,0,0,0},
      {3,8,6672,9489,0,0,0,0,0,0,0,0,0,0},
      {3,9,3170,7457,0,0,0,0,0,0,0,0,0,0},
      {3,10,7868,5731,0,0,0,0,0,0,0,0,0,0},
      {3,11,6121,10732,0,0,0,0,0,0,0,0,0,0},
      {3,12,4843,9132,0,0,0,0,0,0,0,0,0,0},
      {3,13,580,9591,0,0,0,0,0,0,0,0,0,0},
      {3,14,6267,9290,0,0,0,0,0,0,0,0,0,0},
      {3,15,3009,2268,0,0,0,0,0,0,0,0,0,0},
      {3,16,195,2419,0,0,0,0,0,0,0,0,0,0},
      {3,17,8016,1557,0,0,0,0,0,0,0,0,0,0},
      {3,18,1516,9195,0,0,0,0,0,0,0,0,0,0},
      {3,19,8062,9064,0,0,0,0,0,0,0,0,0,0},
      {3,20,2095,8968,0,0,0,0,0,0,0,0,0,0},
      {3,21,753,7326,0,0,0,0,0,0,0,0,0,0},
      {3,22,6291,3833,0,0,0,0,0,0,0,0,0,0},
      {3,23,2614,7844,0,0,0,0,0,0,0,0,0,0},
      {3,24,2303,646,0,0,0,0,0,0,0,0,0,0},
      {3,25,2075,611,0,0,0,0,0,0,0,0,0,0},
      {3,26,4687,362,0,0,0,0,0,0,0,0,0,0},
      {3,27,8684,9940,0,0,0,0,0,0,0,0,0,0},
      {3,28,4830,2065,0,0,0,0,0,0,0,0,0,0},
      {3,29,7038,1363,0,0,0,0,0,0,0,0,0,0},
      {3,0,1769,7837,0,0,0,0,0,0,0,0,0,0},
      {3,1,3801,1689,0,0,0,0,0,0,0,0,0,0},
      {3,2,10070,2359,0,0,0,0,0,0,0,0,0,0},
      {3,3,3667,9918,0,0,0,0,0,0,0,0,0,0},
      {3,4,1914,6920,0,0,0,0,0,0,0,0,0,0},
      {3,5,4244,5669,0,0,0,0,0,0,0,0,0,0},
      {3,6,10245,7821,0,0,0,0,0,0,0,0,0,0},
      {3,7,7648,3944,0,0,0,0,0,0,0,0,0,0},
      {3,8,3310,5488,0,0,0,0,0,0,0,0,0,0},
      {3,9,6346,9666,0,0,0,0,0,0,0,0,0,0},
      {3,10,7088,6122,0,0,0,0,0,0,0,0,0,0},
      {3,11,1291,7827,0,0,0,0,0,0,0,0,0,0},
      {3,12,10592,8945,0,0,0,0,0,0,0,0,0,0},
      {3,13,3609,7120,0,0,0,0,0,0,0,0,0,0},
      {3,14,9168,9112,0,0,0,0,0,0,0,0,0,0},
      {3,15,6203,8052,0,0,0,0,0,0,0,0,0,0},
      {3,16,3330,2895,0,0,0,0,0,0,0,0,0,0},
      {3,17,4264,10563,0,0,0,0,0,0,0,0,0,0},
      {3,18,10556,6496,0,0,0,0,0,0,0,0,0,0},
      {3,19,8807,7645,0,0,0,0,0,0,0,0,0,0},
      {3,20,1999,4530,0,0,0,0,0,0,0,0,0,0},
      {3,21,9202,6818,0,0,0,0,0,0,0,0,0,0},
      {3,22,3403,1734,0,0,0,0,0,0,0,0,0,0},
      {3,23,2106,9023,0,0,0,0,0,0,0,0,0,0},
      {3,24,6881,3883,0,0,0,0,0,0,0,0,0,0},
      {3,25,3895,2171,0,0,0,0,0,0,0,0,0,0},
      {3,26,4062,6424,0,0,0,0,0,0,0,0,0,0},
      {3,27,3755,9536,0,0,0,0,0,0,0,0,0,0},
      {3,28,4683,2131,0,0,0,0,0,0,0,0,0,0},
      {3,29,7347,8027,0,0,0,0,0,0,0,0,0,0}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_1_3S[15][13]=
    {
      {12,416,8909,4156,3216,3112,2560,2912,6405,8593,4969,6723,6912},
      {12,8978,3011,4339,9312,6396,2957,7288,5485,6031,10218,2226,3575},
      {12,3383,10059,1114,10008,10147,9384,4290,434,5139,3536,1965,2291},
      {12,2797,3693,7615,7077,743,1941,8716,6215,3840,5140,4582,5420},
      {12,6110,8551,1515,7404,4879,4946,5383,1831,3441,9569,10472,4306},
      {3,1505,5682,7778,0,0,0,0,0,0,0,0,0},
      {3,7172,6830,6623,0,0,0,0,0,0,0,0,0},
      {3,7281,3941,3505,0,0,0,0,0,0,0,0,0},
      {3,10270,8669,914,0,0,0,0,0,0,0,0,0},
      {3,3622,7563,9388,0,0,0,0,0,0,0,0,0},
      {3,9930,5058,4554,0,0,0,0,0,0,0,0,0},
      {3,4844,9609,2707,0,0,0,0,0,0,0,0,0},
      {3,6883,3237,1714,0,0,0,0,0,0,0,0,0},
      {3,4768,3878,10017,0,0,0,0,0,0,0,0,0},
      {3,10127,3334,8267,0,0,0,0,0,0,0,0,0}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_2_5S[18][13]=
    {
      {12,5650,4143,8750,583,6720,8071,635,1767,1344,6922,738,6658},
      {12,5696,1685,3207,415,7019,5023,5608,2605,857,6915,1770,8016},
      {12,3992,771,2190,7258,8970,7792,1802,1866,6137,8841,886,1931},
      {12,4108,3781,7577,6810,9322,8226,5396,5867,4428,8827,7766,2254},
      {12,4247,888,4367,8821,9660,324,5864,4774,227,7889,6405,8963},
      {12,9693,500,2520,2227,1811,9330,1928,5140,4030,4824,806,3134},
      {3,1652,8171,1435,0,0,0,0,0,0,0,0,0},
      {3,3366,6543,3745,0,0,0,0,0,0,0,0,0},
      {3,9286,8509,4645,0,0,0,0,0,0,0,0,0},
      {3,7397,5790,8972,0,0,0,0,0,0,0,0,0},
      {3,6597,4422,1799,0,0,0,0,0,0,0,0,0},
      {3,9276,4041,3847,0,0,0,0,0,0,0,0,0},
      {3,8683,7378,4946,0,0,0,0,0,0,0,0,0},
      {3,5348,1993,9186,0,0,0,0,0,0,0,0,0},
      {3,6724,9015,5646,0,0,0,0,0,0,0,0,0},
      {3,4502,4439,8474,0,0,0,0,0,0,0,0,0},
      {3,5107,7342,9442,0,0,0,0,0,0,0,0,0},
      {3,1387,8910,2660,0,0,0,0,0,0,0,0,0}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_1_2S[20][9]=
    {
      {8,20,712,2386,6354,4061,1062,5045,5158},
      {8,21,2543,5748,4822,2348,3089,6328,5876},
      {8,22,926,5701,269,3693,2438,3190,3507},
      {8,23,2802,4520,3577,5324,1091,4667,4449},
      {8,24,5140,2003,1263,4742,6497,1185,6202},
      {3,0,4046,6934,0,0,0,0,0},
      {3,1,2855,66,0,0,0,0,0},
      {3,2,6694,212,0,0,0,0,0},
      {3,3,3439,1158,0,0,0,0,0},
      {3,4,3850,4422,0,0,0,0,0},
      {3,5,5924,290,0,0,0,0,0},
      {3,6,1467,4049,0,0,0,0,0},
      {3,7,7820,2242,0,0,0,0,0},
      {3,8,4606,3080,0,0,0,0,0},
      {3,9,4633,7877,0,0,0,0,0},
      {3,10,3884,6868,0,0,0,0,0},
      {3,11,8935,4996,0,0,0,0,0},
      {3,12,3028,764,0,0,0,0,0},
      {3,13,5988,1057,0,0,0,0,0},
      {3,14,7411,3450,0,0,0,0,0}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_3_5S_DVBT2[27][13]=
    {
      {12,71,1478,1901,2240,2649,2725,3592,3708,3965,4080,5733,6198},
      {12,393,1384,1435,1878,2773,3182,3586,5465,6091,6110,6114,6327},
      {12,160,1149,1281,1526,1566,2129,2929,3095,3223,4250,4276,4612},
      {12,289,1446,1602,2421,3559,3796,5590,5750,5763,6168,6271,6340},
      {12,947,1227,2008,2020,2266,3365,3588,3867,4172,4250,4865,6290},
      {3,3324,3704,4447,0,0,0,0,0,0,0,0,0},
      {3,1206,2565,3089,0,0,0,0,0,0,0,0,0},
      {3,529,4027,5891,0,0,0,0,0,0,0,0,0},
      {3,141,1187,3206,0,0,0,0,0,0,0,0,0},
      {3,1990,2972,5120,0,0,0,0,0,0,0,0,0},
      {3,752,796,5976,0,0,0,0,0,0,0,0,0},
      {3,1129,2377,4030,0,0,0,0,0,0,0,0,0},
      {3,6077,6108,6231,0,0,0,0,0,0,0,0,0},
      {3,61,1053,1781,0,0,0,0,0,0,0,0,0},
      {3,2820,4109,5307,0,0,0,0,0,0,0,0,0},
      {3,2088,5834,5988,0,0,0,0,0,0,0,0,0},
      {3,3725,3945,4010,0,0,0,0,0,0,0,0,0},
      {3,1081,2780,3389,0,0,0,0,0,0,0,0,0},
      {3,659,2221,4822,0,0,0,0,0,0,0,0,0},
      {3,3033,6060,6160,0,0,0,0,0,0,0,0,0},
      {3,756,1489,2350,0,0,0,0,0,0,0,0,0},
      {3,3350,3624,5470,0,0,0,0,0,0,0,0,0},
      {3,357,1825,5242,0,0,0,0,0,0,0,0,0},
      {3,585,3372,6062,0,0,0,0,0,0,0,0,0},
      {3,561,1417,2348,0,0,0,0,0,0,0,0,0},
      {3,971,3719,5567,0,0,0,0,0,0,0,0,0},
      {3,1005,1675,2062,0,0,0,0,0,0,0,0,0}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_2_3S[30][14]=
    {
      {13,0,2084,1613,1548,1286,1460,3196,4297,2481,3369,3451,4620,2622},
      {13,1,122,1516,3448,2880,1407,1847,3799,3529,373,971,4358,3108},
      {13,2,259,3399,929,2650,864,3996,3833,107,5287,164,3125,2350},
      {3,3,342,3529,0,0,0,0,0,0,0,0,0,0},
      {3,4,4198,2147,0,0,0,0,0,0,0,0,0,0},
      {3,5,1880,4836,0,0,0,0,0,0,0,0,0,0},
      {3,6,3864,4910,0,0,0,0,0,0,0,0,0,0},
      {3,7,243,1542,0,0,0,0,0,0,0,0,0,0},
      {3,8,3011,1436,0,0,0,0,0,0,0,0,0,0},
      {3,9,2167,2512,0,0,0,0,0,0,0,0,0,0},
      {3,10,4606,1003,0,0,0,0,0,0,0,0,0,0},
      {3,11,2835,705,0,0,0,0,0,0,0,0,0,0},
      {3,12,3426,2365,0,0,0,0,0,0,0,0,0,0},
      {3,13,3848,2474,0,0,0,0,0,0,0,0,0,0},
      {3,14,1360,1743,0,0,0,0,0,0,0,0,0,0},
      {3,0,163,2536,0,0,0,0,0,0,0,0,0,0},
      {3,1,2583,1180,0,0,0,0,0,0,0,0,0,0},
      {3,2,1542,509,0,0,0,0,0,0,0,0,0,0},
      {3,3,4418,1005,0,0,0,0,0,0,0,0,0,0},
      {3,4,5212,5117,0,0,0,0,0,0,0,0,0,0},
      {3,5,2155,2922,0,0,0,0,0,0,0,0,0,0},
      {3,6,347,2696,0,0,0,0,0,0,0,0,0,0},
      {3,7,226,4296,0,0,0,0,0,0,0,0,0,0},
      {3,8,1560,487,0,0,0,0,0,0,0,0,0,0},
      {3,9,3926,1640,0,0,0,0,0,0,0,0,0,0},
      {3,10,149,2928,0,0,0,0,0,0,0,0,0,0},
      {3,11,2364,563,0,0,0,0,0,0,0,0,0,0},
      {3,12,635,688,0,0,0,0,0,0,0,0,0,0},
      {3,13,231,1684,0,0,0,0,0,0,0,0,0,0},
      {3,14,1129,3894,0,0,0,0,0,0,0,0,0,0}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_3_4S[33][13]=
    {
      {12,3,3198,478,4207,1481,1009,2616,1924,3437,554,683,1801},
      {3,4,2681,2135,0,0,0,0,0,0,0,0,0},
      {3,5,3107,4027,0,0,0,0,0,0,0,0,0},
      {3,6,2637,3373,0,0,0,0,0,0,0,0,0},
      {3,7,3830,3449,0,0,0,0,0,0,0,0,0},
      {3,8,4129,2060,0,0,0,0,0,0,0,0,0},
      {3,9,4184,2742,0,0,0,0,0,0,0,0,0},
      {3,10,3946,1070,0,0,0,0,0,0,0,0,0},
      {3,11,2239,984,0,0,0,0,0,0,0,0,0},
      {3,0,1458,3031,0,0,0,0,0,0,0,0,0},
      {3,1,3003,1328,0,0,0,0,0,0,0,0,0},
      {3,2,1137,1716,0,0,0,0,0,0,0,0,0},
      {3,3,132,3725,0,0,0,0,0,0,0,0,0},
      {3,4,1817,638,0,0,0,0,0,0,0,0,0},
      {3,5,1774,3447,0,0,0,0,0,0,0,0,0},
      {3,6,3632,1257,0,0,0,0,0,0,0,0,0},
      {3,7,542,3694,0,0,0,0,0,0,0,0,0},
      {3,8,1015,1945,0,0,0,0,0,0,0,0,0},
      {3,9,1948,412,0,0,0,0,0,0,0,0,0},
      {3,10,995,2238,0,0,0,0,0,0,0,0,0},
      {3,11,4141,1907,0,0,0,0,0,0,0,0,0},
      {3,0,2480,3079,0,0,0,0,0,0,0,0,0},
      {3,1,3021,1088,0,0,0,0,0,0,0,0,0},
      {3,2,713,1379,0,0,0,0,0,0,0,0,0},
      {3,3,997,3903,0,0,0,0,0,0,0,0,0},
      {3,4,2323,3361,0,0,0,0,0,0,0,0,0},
      {3,5,1110,986,0,0,0,0,0,0,0,0,0},
      {3,6,2532,142,0,0,0,0,0,0,0,0,0},
      {3,7,1690,2405,0,0,0,0,0,0,0,0,0},
      {3,8,1298,1881,0,0,0,0,0,0,0,0,0},
      {3,9,615,174,0,0,0,0,0,0,0,0,0},
      {3,10,1648,3112,0,0,0,0,0,0,0,0,0},
      {3,11,1415,2808,0,0,0,0,0,0,0,0,0}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_4_5S[35][4]=
    {
      {3,5,896,1565},
      {3,6,2493,184},
      {3,7,212,3210},
      {3,8,727,1339},
      {3,9,3428,612},
      {3,0,2663,1947},
      {3,1,230,2695},
      {3,2,2025,2794},
      {3,3,3039,283},
      {3,4,862,2889},
      {3,5,376,2110},
      {3,6,2034,2286},
      {3,7,951,2068},
      {3,8,3108,3542},
      {3,9,307,1421},
      {3,0,2272,1197},
      {3,1,1800,3280},
      {3,2,331,2308},
      {3,3,465,2552},
      {3,4,1038,2479},
      {3,5,1383,343},
      {3,6,94,236},
      {3,7,2619,121},
      {3,8,1497,2774},
      {3,9,2116,1855},
      {3,0,722,1584},
      {3,1,2767,1881},
      {3,2,2701,1610},
      {3,3,3283,1732},
      {3,4,168,1099},
      {3,5,3074,243},
      {3,6,3460,945},
      {3,7,2049,1746},
      {3,8,566,1427},
      {3,9,3545,1168}
    };

    const int bbheaderbch_bb_impl::ldpc_tab_5_6S[37][14]=
    {
      {13,3,2409,499,1481,908,559,716,1270,333,2508,2264,1702,2805},
      {3,4,2447,1926,0,0,0,0,0,0,0,0,0,0},
      {3,5,414,1224,0,0,0,0,0,0,0,0,0,0},
      {3,6,2114,842,0,0,0,0,0,0,0,0,0,0},
      {3,7,212,573,0,0,0,0,0,0,0,0,0,0},
      {3,0,2383,2112,0,0,0,0,0,0,0,0,0,0},
      {3,1,2286,2348,0,0,0,0,0,0,0,0,0,0},
      {3,2,545,819,0,0,0,0,0,0,0,0,0,0},
      {3,3,1264,143,0,0,0,0,0,0,0,0,0,0},
      {3,4,1701,2258,0,0,0,0,0,0,0,0,0,0},
      {3,5,964,166,0,0,0,0,0,0,0,0,0,0},
      {3,6,114,2413,0,0,0,0,0,0,0,0,0,0},
      {3,7,2243,81,0,0,0,0,0,0,0,0,0,0},
      {3,0,1245,1581,0,0,0,0,0,0,0,0,0,0},
      {3,1,775,169,0,0,0,0,0,0,0,0,0,0},
      {3,2,1696,1104,0,0,0,0,0,0,0,0,0,0},
      {3,3,1914,2831,0,0,0,0,0,0,0,0,0,0},
      {3,4,532,1450,0,0,0,0,0,0,0,0,0,0},
      {3,5,91,974,0,0,0,0,0,0,0,0,0,0},
      {3,6,497,2228,0,0,0,0,0,0,0,0,0,0},
      {3,7,2326,1579,0,0,0,0,0,0,0,0,0,0},
      {3,0,2482,256,0,0,0,0,0,0,0,0,0,0},
      {3,1,1117,1261,0,0,0,0,0,0,0,0,0,0},
      {3,2,1257,1658,0,0,0,0,0,0,0,0,0,0},
      {3,3,1478,1225,0,0,0,0,0,0,0,0,0,0},
      {3,4,2511,980,0,0,0,0,0,0,0,0,0,0},
      {3,5,2320,2675,0,0,0,0,0,0,0,0,0,0},
      {3,6,435,1278,0,0,0,0,0,0,0,0,0,0},
      {3,7,228,503,0,0,0,0,0,0,0,0,0,0},
      {3,0,1885,2369,0,0,0,0,0,0,0,0,0,0},
      {3,1,57,483,0,0,0,0,0,0,0,0,0,0},
      {3,2,838,1050,0,0,0,0,0,0,0,0,0,0},
      {3,3,1231,1990,0,0,0,0,0,0,0,0,0,0},
      {3,4,1738,68,0,0,0,0,0,0,0,0,0,0},
      {3,5,2392,951,0,0,0,0,0,0,0,0,0,0},
      {3,6,163,645,0,0,0,0,0,0,0,0,0,0},
      {3,7,2644,1704,0,0,0,0,0,0,0,0,0,0}
    };

  } /* namespace dvbt2ll */
} /* namespace gr */

