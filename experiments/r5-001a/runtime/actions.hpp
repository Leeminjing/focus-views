#pragma once
#include "abi.hpp"
#include <map>

namespace r305 {
class Actions {
    std::ofstream out_;
    std::map<std::string,std::uint64_t> next_;
    std::map<std::string,unsigned> states_;
public:
    struct Ticket {std::string view,id,operation;std::uint64_t sequence=0;};
    explicit Actions(const std::filesystem::path& path):out_(path,std::ios::binary){demand(out_.good(),"Action ledger file");}
    void state(const Ticket& t,unsigned number,const std::string& evidence="{}"){
        static const char* names[]={"submitted","accepted","delivered","effect_confirmed","failed"};
        auto found=states_.find(t.id);
        demand((number==0&&found==states_.end())||(found!=states_.end()&&found->second<3&&(number==4||number==found->second+1)),"valid Action lifecycle");
        states_[t.id]=number;
        out_<<"{\"view\":"<<json(t.view)<<",\"sequence\":"<<t.sequence<<",\"action\":"<<json(t.id)<<",\"operation\":"<<json(t.operation)<<",\"state\":"<<json(names[number])<<",\"tickMs\":"<<GetTickCount64()<<",\"evidence\":"<<evidence<<"}\n";out_.flush();
        demand(out_.good(),"Action append");
    }
    Ticket begin(const std::string& view,const std::string& operation){
        const auto sequence=++next_[view];Ticket t{view,view+"-"+std::to_string(sequence),operation,sequence};state(t,0);return t;
    }
    void fail(const Ticket& t,const Error& e){state(t,4,"{\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+"}");}
};
}
