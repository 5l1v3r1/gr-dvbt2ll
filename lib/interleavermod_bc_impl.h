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

#ifndef INCLUDED_DVBT2LL_INTERLEAVERMOD_BC_IMPL_H
#define INCLUDED_DVBT2LL_INTERLEAVERMOD_BC_IMPL_H

#include <dvbt2ll/interleavermod_bc.h>

namespace gr {
  namespace dvbt2ll {

    class interleavermod_bc_impl : public interleavermod_bc
    {
     private:
      int frame_size;
      int signal_constellation;
      int code_rate;
      int nbch;
      int q_val;
      int mod;
      int cyclic_delay;
      int cell_size;
      unsigned char tempu[FRAME_SIZE_NORMAL];
      unsigned char tempv[FRAME_SIZE_NORMAL];

      const static int twist16n[8];
      const static int twist64n[12];
      const static int twist256n[16];

      const static int twist16s[8];
      const static int twist64s[12];
      const static int twist256s[8];

      const static int mux16[8];
      const static int mux64[12];
      const static int mux256[16];

      const static int mux16_35[8];
      const static int mux16_13[8];
      const static int mux16_25[8];
      const static int mux64_35[12];
      const static int mux64_13[12];
      const static int mux64_25[12];
      const static int mux256_35[16];
      const static int mux256_23[16];

      const static int mux256s[8];
      const static int mux256s_13[8];
      const static int mux256s_25[8];

      gr_complex m_qpsk[4];
      gr_complex m_16qam[16];
      gr_complex m_64qam[64];
      gr_complex m_256qam[256];

      int pn_degree;
      int ti_blocks;
      int fec_blocks;
      int permutations[32768];
      int FECBlocksPerSmallTIBlock;
      int FECBlocksPerBigTIBlock;
      int numBigTIBlocks;
      int numSmallTIBlocks;
      int interleaved_items;
      gr_complex *time_interleave;
      gr_complex **cols;

     public:
      interleavermod_bc_impl(dvbt2_framesize_t framesize, dvbt2_code_rate_t rate, dvbt2_constellation_t constellation, dvbt2_rotation_t rotation);
      ~interleavermod_bc_impl();

      // Where all the action really happens
      void forecast (int noutput_items, gr_vector_int &ninput_items_required);

      int general_work(int noutput_items,
           gr_vector_int &ninput_items,
           gr_vector_const_void_star &input_items,
           gr_vector_void_star &output_items);
    };

  } // namespace dvbt2ll
} // namespace gr

#endif /* INCLUDED_DVBT2LL_INTERLEAVERMOD_BC_IMPL_H */

