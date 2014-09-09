/*
   Copyright 2014 Clerk Ma

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301 USA.
*/

#define EXTERN extern
#include "dpx.h"
#include "yandytex.h"

void ensure_pdf_open(void)
{
  if (output_file_name == 0)
  {
    if (job_name == 0)
      open_log_file();

    pack_job_name(".pdf");

    while (!b_open_out(pdf_file))
      prompt_file_name("file name for output", ".pdf");

    output_file_name = b_make_name_string(pdf_file);
  }
}

void pdf_ship_out (pointer p)
{
  integer page_loc;
  char j, k;

  if (tracing_output > 0)
  {
    print_nl("");
    print_ln();
    prints("Completed box being shipped out");
  }

  if (term_offset > max_print_line - 9)
    print_ln();
  else if ((term_offset > 0) || (file_offset > 0))
    print_char(' ');

  print_char('[');
  j = 9;

  while ((count(j) == 0) && (j > 0))
    decr(j);

  for (k = 0; k <= j; k++)
  {
    print_int(count(k));

    if (k < j)
      print_char('.');
  }

  update_terminal();

  if (tracing_output > 0)
  {
    print_char(']');
    begin_diagnostic();
    show_box(p);
    end_diagnostic(true);
  }

  if ((height(p) > max_dimen) || (depth(p) > max_dimen) ||
      (height(p) + depth(p) + v_offset > max_dimen) ||
      (width(p) + h_offset > max_dimen))
  {
    print_err("Huge page cannot be shipped out");
    help2("The page just created is more than 18 feet tall or",
      "more than 18 feet wide, so I suspect something went wrong.");
    error();

    if (tracing_output <= 0)
    {
      begin_diagnostic();
      print_nl("The following box has been deleted:");
      show_box(p);
      end_diagnostic(true);
    }

    goto done;
  }

  if (height(p) + depth(p) + v_offset > max_v)
    max_v = height(p) + depth(p) + v_offset;

  if (width(p) + h_offset > max_h)
    max_h = width(p) + h_offset;

  dvi_h = 0;
  dvi_v = 0;
  cur_h = h_offset;
  dvi_f = null_font;
  ensure_pdf_open();

  if (total_pages == 0)
  {
    pdf_set_version(5);
    pdf_set_compression(9);
    pdf_init_fontmaps();
    read_config_file("dvipdfmx.cfg");
    pdf_doc_set_producer("Y&YTeX 2.3.0");
    pdf_doc_set_creator("TeX");
    pdf_files_init();
    pdf_init_device(0.000015202, 2, 0);
    // TODO: pdfTeX's page width and height.
    // page_width  = pdf_page_width  != 0 ? <- : width(p)             + 2 * (pdf_h_origin + h_offset);
    // page_height = pdf_page_height != 0 ? <- : height(p) + depth(p) + 2 * (pdf_v_origin + v_offset);
    pdf_open_document(pdf_file_name, 0, 595.0, 842.0, 0, 0, (1 << 4));
    spc_exec_at_begin_document();
  }

  page_loc = dvi_offset + dvi_ptr;
  pdf_doc_begin_page(1.0, 72.0, 770.0);
  spc_exec_at_begin_page();

  last_bop = page_loc;
  cur_v = height(p) + v_offset;
  temp_ptr = p;

  if (type(p) == vlist_node)
    pdf_vlist_out();
  else
    pdf_hlist_out();

  spc_exec_at_end_page();
  pdf_doc_end_page();
  incr(total_pages);
  cur_s = -1;

done:
  if (tracing_output <= 0)
    print_char(']');

  dead_cycles = 0;
  update_terminal();

#ifdef STAT
  if (tracing_stats > 1)
  {
    print_nl("Memory usage before: ");
    print_int(var_used);
    print_char('&');
    print_int(dyn_used);
    print_char(';');
  }
#endif

  flush_node_list(p);

#ifdef STAT
  if (tracing_stats > 1)
  {
    prints(" after: ");
    print_int(var_used);
    print_char('&');
    print_int(dyn_used);
    prints("; still utouched: ");
    print_int(hi_mem_min - lo_mem_max - 1);
    print_ln();
  }
#endif
}

void pdf_synch_h (void)
{
  if (cur_h != dvi_h)
    dvi_h = cur_h;
}

void pdf_synch_v (void)
{
  if (cur_v != dvi_v)
    dvi_v = cur_v;
}

int pdf_get_font_id (internal_font_number f)
{
  char * sbuf = malloc(length(font_name[f]) + 1);
  int id;
  memset(sbuf, 0, length(font_name[f]) + 1);
  memcpy(sbuf, str_pool + str_start[font_name[f]], length(font_name[f]));
  id = dvi_locate_font(sbuf, font_size[f]);
  free(sbuf);

  return id;
}

void pdf_out_char (internal_font_number f, ASCII_code c)
{
  pdf_rect rect;
  char cbuf[2];
  cbuf[0] = c;
  cbuf[1] = 0;
  pdf_dev_set_string(cur_h, -cur_v, cbuf, 1, char_width(f, char_info(f, c)), font_id[f], 1);
  pdf_dev_set_rect(&rect, cur_h, -cur_v, char_width(f, char_info(f, c)),
      char_height(f, height_depth(char_info(f, c))),
      char_depth(f, height_depth(char_info(f, c))));
  pdf_doc_expand_box(&rect);
}

void pdf_hlist_out (void)
{
  scaled base_line;
  scaled left_edge;
  scaled save_h, save_v;
  pointer this_box;
  // glue_ord g_order;
  int g_order;
  // char g_sign;
  int g_sign;
  pointer p;
  integer save_loc;
  pointer leader_box;
  scaled leader_wd;
  scaled lx;
  boolean outer_doing_leaders;
  scaled edge;
  real glue_temp;
  real cur_glue;
  scaled cur_g;

  cur_g = 0;
  cur_glue = 0.0;
  this_box = temp_ptr;
  g_order = glue_order(this_box);
  g_sign = glue_sign(this_box);
  p = list_ptr(this_box);
  incr(cur_s);

  if (cur_s > max_push)
    max_push = cur_s;

  save_loc = dvi_offset + dvi_ptr;
  base_line = cur_v;
  left_edge = cur_h;

  while (p != 0)
reswitch:
  if (is_char_node(p))
  {
    pdf_synch_h();
    pdf_synch_v();

    do
    {
      f = font(p);
      c = character(p);

      if (f != dvi_f)
      {
        if (!font_used[f])
        {
          font_used[f] = true;
          font_id[f]   = pdf_get_font_id(f);
        }

        dvi_f = f;
      }

      pdf_out_char(dvi_f, c);
      cur_h = cur_h + char_width(f, char_info(f, c));
      p = link(p);
    } while (!(!is_char_node(p)));

    dvi_h = cur_h;
  }
  else
  {
    switch (type(p))
    {
      case hlist_node:
      case vlist_node:
        if (list_ptr(p) == 0)
          cur_h = cur_h + width(p);
        else
        {
          save_h = dvi_h;
          save_v = dvi_v;
          cur_v = base_line + shift_amount(p);
          temp_ptr = p;
          edge = cur_h;

          if (type(p) == vlist_node)
            pdf_vlist_out();
          else
            pdf_hlist_out();

          dvi_h = save_h;
          dvi_v = save_v;
          cur_h = edge + width(p);
          cur_v = base_line;
        }
        break;

      case rule_node:
        {
          rule_ht = height(p);
          rule_dp = depth(p);
          rule_wd = width(p);
          goto fin_rule;
        }
        break;

      case whatsit_node:
        out_what(p);
        break;

      case glue_node:
        {
          g = glue_ptr(p);
          rule_wd = width(g) - cur_g;

          if (g_sign != normal)
          {
            if (g_sign == stretching)
            {
              if (stretch_order(g) == g_order)
              {
                cur_glue = cur_glue + stretch(g);
                vet_glue(glue_set(this_box) * cur_glue);
                cur_g = round(glue_temp);
              }
            }
            else if (shrink_order(g) == g_order)
            {
              cur_glue = cur_glue - shrink(g);
              vet_glue(glue_set(this_box) * cur_glue);
              cur_g = round(glue_temp);
            }
          }

          rule_wd = rule_wd + cur_g;

          if (subtype(p) >= a_leaders)
          {
            leader_box = leader_ptr(p);

            if (type(leader_box) == rule_node)
            {
              rule_ht = height(leader_box);
              rule_dp = depth(leader_box);
              goto fin_rule;
            }

            leader_wd = width(leader_box);

            if ((leader_wd > 0) && (rule_wd > 0))
            {
              rule_wd = rule_wd + 10;
              edge = cur_h + rule_wd;
              lx = 0;

              if (subtype(p) == a_leaders)
              {
                save_h = cur_h;
                cur_h = left_edge + leader_wd * ((cur_h - left_edge) / leader_wd);

                if (cur_h < save_h)
                  cur_h = cur_h + leader_wd;
              }
              else
              {
                lq = rule_wd / leader_wd;
                lr = rule_wd % leader_wd;

                if (subtype(p) == c_leaders)
                  cur_h = cur_h + (lr / 2);
                else
                {
                  lx = (2 * lr + lq + 1) / (2 * lq + 2);
                  cur_h = cur_h + ((lr - (lq - 1)* lx) / 2);
                }
              }

              while (cur_h + leader_wd <= edge)
              {
                cur_v = base_line + shift_amount(leader_box);
                pdf_synch_v();
                save_v = dvi_v;
                pdf_synch_h();
                save_h = dvi_h;
                temp_ptr = leader_box;
                outer_doing_leaders = doing_leaders;
                doing_leaders = true;

                if (type(leader_box) == vlist_node)
                  pdf_vlist_out();
                else
                  pdf_hlist_out();

                doing_leaders = outer_doing_leaders;
                dvi_v = save_v;
                dvi_h = save_h;
                cur_v = base_line;
                cur_h = save_h + leader_wd + lx;
              }

              cur_h = edge - 10;
              goto next_p;
            }
          }

          goto move_past;
        }
        break;

      case kern_node:
      case math_node:
        cur_h = cur_h + width(p);
        break;

      case ligature_node:
        {
          mem[lig_trick] = mem[lig_char(p)];
          link(lig_trick) = link(p);
          p = lig_trick;
          goto reswitch;
        }
        break;

      default:
        break;
    }

    goto next_p;

fin_rule:
    if (is_running(rule_ht))
      rule_ht = height(this_box);

    if (is_running(rule_dp))
      rule_dp = depth(this_box);

    rule_ht = rule_ht + rule_dp;

    if ((rule_ht > 0) && (rule_wd > 0))
    {
      pdf_synch_h();
      cur_v = base_line + rule_dp;
      pdf_synch_v();
      pdf_dev_set_rule(dvi_h, -dvi_v, rule_wd, rule_ht);
      cur_v = base_line;
      dvi_h = dvi_h + rule_wd;
    }

move_past:
    cur_h = cur_h + rule_wd;

next_p:
    p = link(p);
  }

  prune_movements(save_loc);
  decr(cur_s);
}

void pdf_vlist_out (void)
{
  scaled left_edge;
  scaled top_edge;
  scaled save_h, save_v;
  pointer this_box;
  // glue_ord g_order;
  int g_order;
  // char g_sign;
  int g_sign;
  pointer p;
  integer save_loc;
  pointer leader_box;
  scaled leader_ht;
  scaled lx;
  boolean outer_doing_leaders;
  scaled edge;
  real glue_temp;
  real cur_glue;
  scaled cur_g;

  cur_g = 0;
  cur_glue = 0.0;
  this_box = temp_ptr;
  g_order = glue_order(this_box);
  g_sign = glue_sign(this_box);
  p = list_ptr(this_box);
  incr(cur_s);

  if (cur_s > max_push)
    max_push = cur_s;

  save_loc = dvi_offset + dvi_ptr;
  left_edge = cur_h;
  cur_v = cur_v - height(this_box);
  top_edge = cur_v;

  while (p != 0)
  {
    if (is_char_node(p))
    {
      confusion("vlistout");
      return;
    }
    else
    {
      switch (type(p))
      {
        case hlist_node:
        case vlist_node:
          if (list_ptr(p) == 0)
            cur_v = cur_v + height(p) + depth(p);
          else
          {
            cur_v = cur_v + height(p);
            pdf_synch_v();
            save_h = dvi_h;
            save_v = dvi_v;
            cur_h = left_edge + shift_amount(p);
            temp_ptr = p;

            if (type(p) == vlist_node)
              pdf_vlist_out();
            else
              pdf_hlist_out();

            dvi_h = save_h;
            dvi_v = save_v;
            cur_v = save_v + depth(p);
            cur_h = left_edge;
          }
          break;

        case rule_node:
          {
            rule_ht = height(p);
            rule_dp = depth(p);
            rule_wd = width(p);
            goto fin_rule;
          }
          break;

        case whatsit_node:
          out_what(p);
          break;

        case glue_node:
          {
            g = glue_ptr(p);
            rule_ht = width(g) - cur_g;

            if (g_sign != normal)
            {
              if (g_sign == stretching)
              {
                if (stretch_order(g) == g_order)
                {
                  cur_glue = cur_glue + stretch(g);
                  vet_glue(glue_set(this_box) * cur_glue);
                  cur_g = round(glue_temp);
                }
              }
              else if (shrink_order(g) == g_order)   /* BUG FIX !!! */
              {
                cur_glue = cur_glue - shrink(g);
                vet_glue(glue_set(this_box) * cur_glue);
                cur_g = round(glue_temp);
              }
            }

            rule_ht = rule_ht + cur_g;

            if (subtype(p) >= a_leaders)
            {
              leader_box = leader_ptr(p);

              if (type(leader_box) == rule_node)
              {
                rule_wd = width(leader_box);
                rule_dp = 0;
                goto fin_rule;
              }

              leader_ht = height(leader_box) + depth(leader_box);

              if ((leader_ht > 0) && (rule_ht > 0))
              {
                rule_ht = rule_ht + 10;
                edge = cur_v + rule_ht;
                lx = 0;

                if (subtype(p) == a_leaders)
                {
                  save_v = cur_v;
                  cur_v = top_edge + leader_ht * ((cur_v - top_edge) / leader_ht);

                  if (cur_v < save_v)
                    cur_v = cur_v + leader_ht;
                }
                else
                {
                  lq = rule_ht / leader_ht;
                  lr = rule_ht % leader_ht;

                  if (subtype(p) == c_leaders)
                    cur_v = cur_v + (lr / 2);
                  else
                  {
                    lx = (2 * lr + lq + 1) / (2 * lq + 2);
                    cur_v = cur_v + ((lr - (lq - 1) * lx) / 2);
                  }
                }

                while (cur_v + leader_ht <= edge)
                {
                  cur_h = left_edge + shift_amount(leader_box);
                  pdf_synch_h();
                  save_h = dvi_h;
                  cur_v = cur_v + height(leader_box);
                  pdf_synch_v();
                  save_v = dvi_v;
                  temp_ptr = leader_box;
                  outer_doing_leaders = doing_leaders;
                  doing_leaders = true;

                  if (type(leader_box) == vlist_node)
                    pdf_vlist_out();
                  else
                    pdf_hlist_out();

                  doing_leaders = outer_doing_leaders;
                  dvi_v = save_v;
                  dvi_h = save_h;
                  cur_h = left_edge;
                  cur_v = save_v - height(leader_box) + leader_ht + lx;
                }

                cur_v = edge - 10;
                goto next_p;
              }
            }

            goto move_past;
          }
          break;

        case kern_node:
          cur_v = cur_v + width(p);
          break;

        default:
          break;
      }

      goto next_p;

fin_rule:
      if (is_running(rule_wd))
        rule_wd = width(this_box);

      rule_ht = rule_ht + rule_dp;
      cur_v = cur_v + rule_ht;

      if ((rule_ht > 0) && (rule_wd > 0))
      {
        pdf_synch_h();
        pdf_synch_v();
        pdf_dev_set_rule(dvi_h, -dvi_v, rule_wd, rule_ht);
      }

      goto next_p;

move_past:
      cur_v = cur_v + rule_ht;
    }

next_p:
    p = link(p);
  }

  prune_movements(save_loc);
  decr(cur_s);
}
