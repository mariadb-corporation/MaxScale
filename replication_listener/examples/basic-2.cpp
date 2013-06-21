#include "binlog_api.h"

#include <iostream>
#include <map>
#include <string>

/*
  Here is a basic system using the event loop to fetch context events
  and store them in an associative array.
 */
using mysql::Binary_log;
using mysql::system::create_transport;
using mysql::system::get_event_type_str;
using mysql::User_var_event;

/**
 * Class to maintain variable values.
 */
template <class AssociativeContainer>
class Save_variables : public Content_handler {
public:
  Save_variables(AssociativeContainer& container)
  : m_var(container)
  {
  }

  Binary_log_event *process_event(User_var_event *event) {
    m_var[event->name] = event->value;
    return NULL;
  }

private:
  AssociativeContainer &m_var;
};


template <class AssociativeContainer>
class Replace_variables : public Content_handler {
public:
  Replace_variables(AssociativeContainer& variables)
    : m_var(variables)
  {
  }

  Binary_log_event *process_event(Query_event *event) {
    std::string *query = &event->query;
    size_t start, end = 0;
    while (true) {
      start = query->find_first_of("@", end);
      if (start == std::string::npos)
        break;
      end = query->find_first_not_of("abcdefghijklmnopqrstuvwxyz", start+1);
      std::string key = query->substr(start + 1, end - start - 1);
      query->replace(start, end - start, "'" + m_var[key] + "'");
    }
    return event;
  }
private:
  AssociativeContainer &m_var;
};


int main(int argc, char** argv) {
  typedef std::map<std::string, std::string> Map;

  if (argc != 2) {
    std::cerr << "Usage: basic-2 <uri>" << std::endl;
    exit(2);
  }

  Binary_log binlog(create_transport(argv[1]));
  binlog.connect();

  binlog.set_position(4);

  Map variables;
  Save_variables<Map> save_variables(variables);
  binlog.content_handler_pipeline()->push_back(&save_variables);
  Replace_variables<Map> replace_variables(variables);
  binlog.content_handler_pipeline()->push_back(&replace_variables);

  while (true) {
    Binary_log_event *event;
    int result = binlog.wait_for_next_event(&event);
    if (result == ERR_EOF)
      break;
    switch (event->get_event_type()) {
    case QUERY_EVENT:
      std::cout << static_cast<Query_event*>(event)->query
                << std::endl;
      break;
    }
  }
}
