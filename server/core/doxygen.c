/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file doxygen.c  - The MaxScale model for Doxygen directed comment blocks
 *
 * This file is not built in to MaxScale at all, it exists only as a model
 * and is intended to have parts of it copied into new code, or existing
 * code that is being upgraded.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                 Description
 * 05/02/2016   Martin Brampton     Initial implementation
 * @endverbatim
 */

/**
 * @brief Example showing how to document a function with Doxygen.
 *
 * Description of what the function does. This part may refer to the parameters
 * of the function, like @p param1 or @p param2. A word of code can also be
 * inserted like @c this which is equivalent to <tt>this</tt> and can be useful
 * to say that the function returns a @c void or an @c int. If you want to have
 * more than one word in typewriter font, then just use @<tt@>.
 * We can also include text verbatim,
 * @verbatim like this@endverbatim
 * Sometimes it is also convenient to include an example of usage:
 * @code
 * BoxStruct *out = Box_The_Function_Name(param1, param2);
 * printf("something...\n");
 * @endcode
 * Or,
 * @code{.py}
 * pyval = python_func(arg1, arg2)
 * print pyval
 * @endcode
 * when the language is not the one used in the current source file (but
 * <b>be careful</b> as this may be supported only by recent versions
 * of Doxygen). By the way, <b>this is how you write bold text</b> or,
 * if it is just one word, then you can just do @b this.
 * @param param1 Description of the first parameter of the function.
 * @param param2 The second one, which follows @p param1.
 * @return Describe what the function returns.
 * @see Box_The_Second_Function
 * @see Box_The_Last_One
 * @see http://website/
 * @note Something to note.
 * @warning Warning.
 */
BOXEXPORT BoxStruct *
Box_The_Function_Name(BoxParamType1 param1, BoxParamType2 param2 /*, ...*/);

/**
 * @brief A simple stub function to show how links do work.
 *
 * Links are generated automatically for webpages (like http://www.google.co.uk)
 * and for structures, like BoxStruct_struct. For typedef-ed types use
 * #BoxStruct.
 * For functions, automatic links are generated when the parenthesis () follow
 * the name of the function, like Box_The_Function_Name().
 * Alternatively, you can use #Box_The_Function_Name.
 * @return @c NULL is always returned.
 */
BOXEXPORT void *
Box_The_Second_Function(void);
