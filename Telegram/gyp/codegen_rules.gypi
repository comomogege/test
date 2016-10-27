# This file is part of Telegram Desktop,
# the official desktop version of Telegram messaging app, see https://telegram.org
#
# Telegram Desktop is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# It is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# In addition, as a special exception, the copyright holders give permission
# to link the code of portions of this program with the OpenSSL library.
#
# Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
# Copyright (c) 2014 John Preston, https://desktop.telegram.org

{
  'actions': [{
    'action_name': 'update_sprites',
    'inputs': [
      '<(PRODUCT_DIR)/codegen_style<(exe_ext)',
      '<(res_loc)/basic.style',
      '<(res_loc)/art/sprite.png',
      '<(res_loc)/art/sprite_200x.png',
    ],
    'outputs': [
      '<(res_loc)/art/sprite_125x.png',
      '<(res_loc)/art/sprite_150x.png',
    ],
    'action': [
      '<(PRODUCT_DIR)/codegen_style<(exe_ext)',
      '-I<(res_loc)', '-I<(src_loc)',
      '--skip-styles', '<(res_loc)/basic.style',
    ],
    'message': 'Updating sprites..',
  }, {
    'action_name': 'update_dependent_styles',
    'inputs': [
      '<(DEPTH)/update_dependent.py',
      '<@(style_files)',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/update_dependent_styles.timestamp',
    ],
    'action': [
      'python', '<(DEPTH)/update_dependent.py', '-tstyle',
      '-I<(res_loc)', '-I<(src_loc)',
      '-o<(SHARED_INTERMEDIATE_DIR)/update_dependent_styles.timestamp',
      '<@(style_files)',
    ],
    'message': 'Updating dependent style files..',
  }, {
    'action_name': 'update_dependent_qrc',
    'inputs': [
      '<(DEPTH)/update_dependent.py',
      '<@(qrc_files)',
      '<!@(python <(DEPTH)/update_dependent.py -tqrc_list <@(qrc_files))',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/update_dependent_qrc.timestamp',
    ],
    'action': [
      'python', '<(DEPTH)/update_dependent.py', '-tqrc',
      '-o<(SHARED_INTERMEDIATE_DIR)/update_dependent_qrc.timestamp',
      '<@(qrc_files)',
    ],
    'message': 'Updating dependent qrc files..',
  }, {
    'action_name': 'codegen_lang',
    'inputs': [
      '<(PRODUCT_DIR)/MetaLang<(exe_ext)',
      '<(res_loc)/langs/lang.strings',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/lang_auto.cpp',
      '<(SHARED_INTERMEDIATE_DIR)/lang_auto.h',
    ],
    'action': [
      '<(PRODUCT_DIR)/MetaLang<(exe_ext)',
      '-lang_in', '<(res_loc)/langs/lang.strings',
      '-lang_out', '<(SHARED_INTERMEDIATE_DIR)/lang_auto',
    ],
    'message': 'codegen_lang-ing lang.strings..',
    'process_outputs_as_sources': 1,
  }, {
    'action_name': 'codegen_numbers',
    'inputs': [
      '<(PRODUCT_DIR)/codegen_numbers<(exe_ext)',
      '<(res_loc)/numbers.txt',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/numbers.cpp',
      '<(SHARED_INTERMEDIATE_DIR)/numbers.h',
    ],
    'action': [
      '<(PRODUCT_DIR)/codegen_numbers<(exe_ext)',
      '-o<(SHARED_INTERMEDIATE_DIR)', '<(res_loc)/numbers.txt',
    ],
    'message': 'codegen_numbers-ing numbers.txt..',
    'process_outputs_as_sources': 1,
  }],
  'rules': [{
    'rule_name': 'codegen_style',
    'extension': 'style',
    'inputs': [
      '<(PRODUCT_DIR)/codegen_style<(exe_ext)',
      '<(SHARED_INTERMEDIATE_DIR)/update_dependent_styles.timestamp',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/styles/style_<(RULE_INPUT_ROOT).h',
      '<(SHARED_INTERMEDIATE_DIR)/styles/style_<(RULE_INPUT_ROOT).cpp',
    ],
    'action': [
      '<(PRODUCT_DIR)/codegen_style<(exe_ext)',
      '-I<(res_loc)', '-I<(src_loc)', '--skip-sprites',
      '-o<(SHARED_INTERMEDIATE_DIR)/styles',
      '-w<(PRODUCT_DIR)/../..',

      # GYP/Ninja bug workaround: if we specify just <(RULE_INPUT_PATH)
      # the <(RULE_INPUT_ROOT) variables won't be available in Ninja,
      # and the 'message' will be just 'codegen_style-ing .style..'
      # Looks like the using the <(RULE_INPUT_ROOT) here "exports" it
      # for using in the 'message' field.

      '<(RULE_INPUT_DIRNAME)/<(RULE_INPUT_ROOT)<(RULE_INPUT_EXT)',
    ],
    'message': 'codegen_style-ing <(RULE_INPUT_ROOT).style..',
    'process_outputs_as_sources': 1,
  }],
}
