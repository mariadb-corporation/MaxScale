/*
Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights
reserved.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2 of
the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
02110-1301  USA
*/

#include "globals.h"
#include "table_delete.h"

#include <CLucene.h>

CL_NS_USE(index)
CL_NS_USE(util)
CL_NS_USE(store)
CL_NS_USE(search)
CL_NS_USE(document)
CL_NS_USE(queryParser)
CL_NS_USE(analysis)
CL_NS_USE2(analysis,standard)

void table_delete(std::string table_name, mysql::Row_of_fields &fields)
{

  mysql::Row_of_fields::iterator field_it= fields.begin();
  /*
   * First column must be an integer key value
  */
  if (!(field_it->type() == mysql::system::MYSQL_TYPE_LONG ||
      field_it->type() == mysql::system::MYSQL_TYPE_SHORT ||
      field_it->type() == mysql::system::MYSQL_TYPE_LONGLONG))
   return;

  int field_id= 0;
  std::string key;
  std::string combined_key;
  mysql::Converter converter;
  converter.to(key, *field_it);
  combined_key.append (table_name);
  combined_key.append ("_");
  combined_key.append (key);
  do {
    /*
      Each row contains a vector of Value objects. The converter
      allows us to transform the value into another
      representation.
      Only index fields which might contain searchable information.
    */
    if (field_it->type() == mysql::system::MYSQL_TYPE_VARCHAR ||
        field_it->type() == mysql::system::MYSQL_TYPE_MEDIUM_BLOB ||
        field_it->type() == mysql::system::MYSQL_TYPE_BLOB)
    {
      std::string str;
      converter.to(str, *field_it);
      StandardAnalyzer an;
      IndexReader *reader;
      /*
       * Create a Lucene index writer
       */
      if ( IndexReader::indexExists(cl_index_file.c_str()) )
      {
        if ( IndexReader::isLocked(cl_index_file.c_str()) )
        {
          std::cout << "Index was locked; unlocking it."
                    << std::endl;
          IndexReader::unlock(cl_index_file.c_str());
        }
        reader= IndexReader::open(cl_index_file.c_str());
      }

      std::cout << "Deleting index '"
                << combined_key
                << "'" << std::endl;
      TCHAR *combined_key_w= STRDUP_AtoW(combined_key.c_str ());
      Term uniqueKey(_T("id"),combined_key_w);
      reader->deleteDocuments(&uniqueKey);
      delete combined_key_w;
      reader->close();
      delete reader;
      break;
    }
  } while(++field_it != fields.end());
}
