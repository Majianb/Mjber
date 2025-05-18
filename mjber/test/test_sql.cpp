#include <iostream>

#include "../tools/mysql_poll.h"

int main(){
    auto poll = MySQLConnectionPool(
        "154.8.147.200",
        "remoteuser",
        "Mjbshuai123!"
    );
    poll.executeQuery("SELECT * FROM testdb.users;");
}

