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
#include "table_insert.h"

#include <stdlib.h>
#include <CLucene.h>
#include <boost/foreach.hpp>

CL_NS_USE(index)
CL_NS_USE(util)
CL_NS_USE(store)
CL_NS_USE(search)
CL_NS_USE(document)
CL_NS_USE(queryParser)
CL_NS_USE(analysis)
CL_NS_USE2(analysis,standard)

void table_insert(std::string table_name, mysql::Row_of_fields &fields)
{
  mysql::Row_of_fields::iterator field_it= fields.begin();
  /*
   * First column must be an integer key value
   */
  if (!(field_it->type() == mysql::system::MYSQL_TYPE_LONG ||
      field_it->type() == mysql::system::MYSQL_TYPE_SHORT ||
      field_it->type() == mysql::system::MYSQL_TYPE_LONGLONG))
   return;

  Document *doc= new Document();
  IndexWriter* writer = NULL;
	StandardAnalyzer an;
  mysql::Converter converter;
  bool found_searchable= false;
  int col= 0;
  TCHAR *w_table_name;
  TCHAR *w_str;
  TCHAR *w_key_str;
  TCHAR *w_combined_key;
  std::string aggstr;

  /*
   * Create a Lucene index writer
   */
	if ( IndexReader::indexExists(cl_index_file.c_str()) )
  {
    if ( IndexReader::isLocked(cl_index_file.c_str()) )
    {
      printf("Index was locked... unlocking it.\n");
      IndexReader::unlock(cl_index_file.c_str());
    }
    writer = new IndexWriter( cl_index_file.c_str(), &an, false);
  }else{
		writer = new IndexWriter( cl_index_file.c_str() ,&an, true);
	}
	writer->setMaxFieldLength(IndexWriter::DEFAULT_MAX_FIELD_LENGTH);

  /*
   * Save the presumed table key for later use when we discover if this row
   * should be indexed.
   */
  std::string key;
  converter.to(key, *field_it);

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
      if (!found_searchable)
      {
        std::string combined_key;
        combined_key.append(table_name);
        combined_key.append("_");
        combined_key.append(key);
        w_table_name= STRDUP_AtoW(table_name.c_str());
        Field *table_field= new Field(_T("table"),w_table_name, Field::STORE_YES | Field::INDEX_UNTOKENIZED);
        doc->add( *table_field );
        found_searchable= true;
        w_key_str= STRDUP_AtoW(key.c_str());
        Field *key_field= new Field(_T("row_id"),w_key_str, Field::STORE_YES | Field::INDEX_UNTOKENIZED);
        doc->add(*key_field);
        w_combined_key= STRDUP_AtoW(combined_key.c_str());
        Field *combined_key_field= new Field(_T("id"),w_combined_key, Field::STORE_YES | Field::INDEX_UNTOKENIZED);
        doc->add(*combined_key_field);
      }
      /*
       * Aggregate all searchable information into one string. The key is the
       * qualified table name.
       */
      aggstr.append(" "); // This separator helps us loosing important tokens.
      aggstr.append(str);
      ++col;
     }
  } while(++field_it != fields.end());
  if (found_searchable)
  {
    std::cout << "Indexing "
              << aggstr.length()
              << " characters in table '"
              << table_name
              << "' using key value '"
              << key
              << "'."
              << std::endl;
    std::cout.flush ();
    w_str= STRDUP_AtoW(aggstr.c_str());
    Field *content_field= new Field(_T("text"),w_str, Field::STORE_YES | Field::INDEX_TOKENIZED);
    doc->add( *content_field );
  }
  writer->addDocument(doc);
  writer->close();

  /*
   * Clean up dynamic allocations during indexing
   */
  if (found_searchable)
  {
    free(w_table_name);
    free(w_str);
    free(w_key_str);
    free(w_combined_key);
  }
  delete(doc);
  delete(writer);
}
