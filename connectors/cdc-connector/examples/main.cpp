/**
 * This is an example on how to use the CDC Connector to print the keys and
 * their values.
 */

#include "../cdc_connector.h"
#include <iostream>

int main(int argc, char** argv)
{

    if (argc < 6)
    {
        std::cout << "Usage: HOST PORT USER PASSWORD DATABASE.TABLE" << std::endl;
        std::cout << std::endl;
        std::cout << "Note that DATABASE.TABLE must have both database and table " << std::endl;
        std::cout << "combined together as one value with a period." << std::endl;
        std::cout << std::endl;
        return 1;
    }

    CDC::Connection conn(argv[1],       // Host
                         atoi(argv[2]), // Port
                         argv[3],       // User
                         argv[4]);      // Password

    if (conn.connect(argv[5]))
    {
        CDC::SRow row;

        while ((row = conn.read()))
        {
            for (size_t i = 0; i < row->length(); i++)
            {
                if (i != 0)
                {
                    std::cout << ", ";
                }
                std::cout << row->key(i) << ": " << row->value(i);
            }

            std::cout << std::endl;
        }
    }
    else
    {
        std::cout << conn.error() << std::endl;
    }

    return 0;
}
