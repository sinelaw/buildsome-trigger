#include "job.h"

void Job::execute(
        std::function<void(std::string,
                           std::function<void(void)>)> resolve_input_cb,
        std::function<void(Outcome)> completion_cb)
{

    
}
