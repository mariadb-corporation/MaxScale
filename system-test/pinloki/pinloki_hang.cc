#include <maxtest/testconnections.hh>
#include "test_base.hh"

class HangTest : public TestCase
{
public:
    using TestCase::TestCase;

    void run() override
    {
        std::thread thr(&HangTest::change_master, this);

        for (int i = 0; i < 50 && test.ok(); i++)
        {
            test.check_maxctrl("show threads");
        }

        m_running = false;
        thr.join();
    }


private:
    void change_master()
    {
        maxscale.set_timeout(10);
        maxscale.connect();

        while (m_running && test.ok())
        {
            test.expect(maxscale.query("STOP SLAVE"),
                        "STOP SLAVE failed: %s", maxscale.error());
            test.expect(maxscale.query(change_master_sql(test.repl->ip(0), test.repl->port[0])),
                        "CHANGE MASTER failed: %s", maxscale.error());
            test.expect(maxscale.query("START SLAVE"),
                        "START SLAVE failed: %s", maxscale.error());
        }
    }

    std::atomic<bool> m_running {true};
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return HangTest(test).result();
}
