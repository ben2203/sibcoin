
#include "rpcserver.h"

#include "clientversion.h"
#include "main.h"
#include "net.h"
#include "init.h"
#include "netbase.h"
#include "protocol.h"
#include "sync.h"
#include "timedata.h"
#include "util.h"
#include "utilstrencodings.h"
#include "version.h"
#include <univalue.h>
#include "streams.h"
#include <algorithm>
#include "dexoffer.h"
#include "random.h"
#include "dex/dexdb.h"
#include "dex.h"
#include "dex/dexdto.h"
#include "core_io.h"
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>

#include "dextransaction.h"
#include "db/countryiso.h"
#include "db/currencyiso.h"
#include "db/defaultdatafordb.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif


using namespace std;



UniValue dexoffers(const UniValue& params, bool fHelp)
{
    if (!fTxIndex) {
        throw runtime_error(
            "To use this feture please enable -txindex and make -reindex.\n"
        );
    }

    if (dex::DexDB::self() == 0) {
        throw runtime_error(
            "DexDB is not initialized.\n"
        );
    }

    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "dexoffers [buy|sell|all] [country] [currency] [payment_method]\n"
            "Get DEX offers list.\n"

            "\nArguments:\n"
            "NOTE: Any of the parameters may be skipped.You must specify at least one parameter.\n"
            "\tcountry         (string, optional) two-letter country code (ISO 3166-1 alpha-2 code).\n"
            "\tcurrency        (string, optional) three-letter currency code (ISO 4217).\n"
            "\tpayment_method  (string, optional, case insensitive) payment method name.\n"

            "\nResult (for example):\n"
            "[\n"
            "   {\n"
            "     \"type\"          : \"sell\",   offer type, buy or sell\n"
            "     \"idTransaction\" : \"<id>\",   transaction with offer fee\n"
            "     \"hash\"          : \"<hash>\", offer hash\n"
            "     \"countryIso\"    : \"RU\",     country (ISO 3166-1 alpha-2)\n"
            "     \"currencyIso\"   : \"RUB\",    currency (ISO 4217)\n"
            "     \"paymentMethod\" : 1,        payment method code (default 1 - cash, 128 - online)\n"
            "     \"price\"         : 10000,\n"
            "     \"minAmount\"     : 1000,\n"
            "     \"timeCreate\"    : 94766313939344,\n"
            "     \"timeExpiration\": 10,       offer expiration (in days)\n"
            "     \"shortInfo\"     : \"...\",    offer short info (max 140 bytes)\n"
            "     \"details\"       : \"...\"     offer details (max 1024 bytes)\n"
            "   },\n"
            "   ...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("dexoffers", "all USD")
            + HelpExampleCli("dexoffers", "RU RUB cash")
            + HelpExampleCli("dexoffers", "all USD online")
        );

    UniValue result(UniValue::VARR);

    std::string typefilter, countryfilter, currencyfilter;
    std::string methodfilter = "*";
    unsigned char methodfiltertype = 0;
    std::list<std::string> words {"buy", "sell", "all"};
    dex::CountryIso  countryiso;
    dex::CurrencyIso currencyiso;

    for (size_t i = 0; i < params.size(); i++) {
        if (i == 0 && typefilter.empty()) {
            if (std::find(words.begin(), words.end(), params[0].get_str()) != words.end()) {
                typefilter = params[0].get_str();
                continue;
            } else {
                typefilter = "all";
            }
        }
        if (i < 2 && countryfilter.empty()) {
            if (countryiso.isValid(params[i].get_str())) {
                countryfilter = params[i].get_str();
                continue;
            } else {
                countryfilter = "*";
            }
        }
        if (i < 3 && currencyfilter.empty()) {
            if (currencyiso.isValid(params[i].get_str())) {
                currencyfilter = params[i].get_str();
                continue;
            } else {
                currencyfilter = "*";
            }
        }
        {
            methodfilter.clear();
            std::string methodname = boost::algorithm::to_lower_copy(params[i].get_str());
            std::list<dex::PaymentMethodInfo> pms = dex::DexDB::self()->getPaymentMethodsInfo();
            for (auto j : pms) {
                std::string name = boost::algorithm::to_lower_copy(j.name);
                if (name == methodname) {
                    methodfilter = j.name;
                    methodfiltertype = j.type;
                }
            }

            if (methodfilter.empty()) {
                throw runtime_error("\nwrong parameter: " + params[i].get_str() + "\n");
            }
        }
    }

    if (currencyfilter.empty()) currencyfilter = "*";
    if (countryfilter.empty()) countryfilter = "*";

    if (countryfilter.empty() || currencyfilter.empty() || typefilter.empty() || methodfilter.empty()) {
        throw runtime_error("\nwrong parameters\n");
    }

    // check country and currency in DB
    if (countryfilter != "*") {
        dex::CountryInfo  countryinfo = dex::DexDB::self()->getCountryInfo(countryfilter);
        if (!countryinfo.enabled) {
            throw runtime_error("\nERROR: this country is disabled in DB\n");
        }
    }
    if (currencyfilter != "*") {
        dex::CurrencyInfo  currencyinfo = dex::DexDB::self()->getCurrencyInfo(currencyfilter);
        if (!currencyinfo.enabled) {
            throw runtime_error("\nERROR: this currency is disabled in DB\n");
        }
    }



    if (typefilter == "buy" || typefilter == "all") {
        std::list<dex::OfferInfo> offers = dex::DexDB::self()->getOffersBuy();
        for (auto i : offers) {
          if (countryfilter  != "*" && countryfilter    != i.countryIso   ) continue;
          if (currencyfilter != "*" && currencyfilter   != i.currencyIso  ) continue;
          if (methodfilter   != "*" && methodfiltertype != i.paymentMethod) continue;
          CDexOffer o(i, dex::Buy);
          result.push_back(o.getUniValue());
        }
    }

    if (typefilter == "sell" || typefilter == "all") {
        std::list<dex::OfferInfo> offers = dex::DexDB::self()->getOffersSell();
        for (auto i : offers) {
          if (countryfilter  != "*" && countryfilter    != i.countryIso   ) continue;
          if (currencyfilter != "*" && currencyfilter   != i.currencyIso  ) continue;
          if (methodfilter   != "*" && methodfiltertype != i.paymentMethod) continue;
          CDexOffer o(i, dex::Sell);
          result.push_back(o.getUniValue());
        }
    }

    return result;
}




UniValue dexmyoffers(const UniValue& params, bool fHelp)
{
    if (!fTxIndex) {
        throw runtime_error(
            "To use this feture please enable -txindex and make -reindex.\n"
        );
    }
    if (dex::DexDB::self() == 0) {
        throw runtime_error(
            "DexDB is not initialized.\n"
        );
    }
    if (fHelp || params.size() < 1 || params.size() > 5)
        throw runtime_error(
            "dexmyoffers [buy|sell|all] [country] [currency] [payment_method] [status]\n"
            "Return a list of  DEX own offers.\n"

            "\nArguments:\n"
            "NOTE: Any of the parameters may be skipped.You must specify at least one parameter.\n"
            "\tcountry         (string, optional) two-letter country code (ISO 3166-1 alpha-2 code).\n"
            "\tcurrency        (string, optional) three-letter currency code (ISO 4217).\n"
            "\tpayment_method  (string, optional, case insensitive) payment method name.\n"
            "\tstatus          (string, optional, case insensitive) offer status (Active,Draft,Expired,Cancelled,Suspended,Unconfirmed).\n"

            "\nResult (for example):\n"
            "[\n"
            "   {\n"
            "     \"type\"          : \"sell\",   offer type, buy or sell\n"
            "     \"status\"        : \"1\",      offer status\n"
            "     \"statusStr\"     : \"Draft\",  offer status name\n"
            "     \"idTransaction\" : \"<id>\",   transaction with offer fee\n"
            "     \"hash\"          : \"<hash>\", offer hash\n"
            "     \"pubKey\"        : \"<key>\",  offer public key\n"
            "     \"countryIso\"    : \"RU\",     country (ISO 3166-1 alpha-2)\n"
            "     \"currencyIso\"   : \"RUB\",    currency (ISO 4217)\n"
            "     \"paymentMethod\" : 1,        payment method code (default 1 - cash, 128 - online)\n"
            "     \"price\"         : 10000,\n"
            "     \"minAmount\"     : 1000,\n"
            "     \"timeCreate\"    : 94766313939344,\n"
            "     \"timeExpiration\": 10,       offer expiration (in days)\n"
            "     \"shortInfo\"     : \"...\",    offer short info (max 140 bytes)\n"
            "     \"details\"       : \"...\"     offer details (max 1024 bytes)\n"
            "   },\n"
            "   ...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("dexmyoffers", "all USD")
            + HelpExampleCli("dexmyoffers", "RU RUB cash")
            + HelpExampleCli("dexmyoffers", "all USD online")
        );

    UniValue result(UniValue::VARR);

    std::string typefilter, countryfilter, currencyfilter, methodfilter;
    std::string statusfilter = "*";
    int statusval = -1;
    unsigned char methodfiltertype = 0;
    std::list<std::string> words {"buy", "sell", "all"};
    std::vector<std::string> statuses { "active","draft","expired","cancelled","suspended","unconfirmed"};
    dex::CountryIso  countryiso;
    dex::CurrencyIso currencyiso;

    for (size_t i = 0; i < params.size(); i++) {
        if (i == 0 && typefilter.empty()) {
            if (std::find(words.begin(), words.end(), params[0].get_str()) != words.end()) {
                typefilter = params[0].get_str();
                continue;
            } else {
                typefilter = "all";
            }
        }
        if (i < 2 && countryfilter.empty()) {
            if (countryiso.isValid(params[i].get_str())) {
                countryfilter = params[i].get_str();
                continue;
            } else {
                countryfilter = "*";
            }
        }
        if (i < 3 && currencyfilter.empty()) {
            if (currencyiso.isValid(params[i].get_str())) {
                currencyfilter = params[i].get_str();
                continue;
            } else {
                currencyfilter = "*";
            }
        }

        if (methodfilter.empty()) {
            std::string methodname = boost::algorithm::to_lower_copy(params[i].get_str());
            std::list<dex::PaymentMethodInfo> pms = dex::DexDB::self()->getPaymentMethodsInfo();
            for (auto j : pms) {
                std::string name = boost::algorithm::to_lower_copy(j.name);
                if (name == methodname) {
                    methodfilter = j.name;
                    methodfiltertype = j.type;
                    continue;
                }
            }
        }

        {
            statusfilter.clear();
            std::string statusname = boost::algorithm::to_lower_copy(params[i].get_str());
            for (size_t j = 0; j < statuses.size(); j++) {
                if (statuses[j] == statusname)  {
                    statusfilter = statusname;
                    statusval = j;
                    continue;
                }
            }

            if (statusfilter.empty()) {
                throw runtime_error("\nwrong parameter: " + params[i].get_str() + "\n");
            }
        }
    }

    if (currencyfilter.empty()) currencyfilter = "*";
    if ( countryfilter.empty()) countryfilter  = "*";
    if (  methodfilter.empty()) methodfilter   = "*";


    if (countryfilter.empty() || currencyfilter.empty() || typefilter.empty() || methodfilter.empty() || statusfilter.empty()) {
        throw runtime_error("\nwrong parameters\n");
    }

    // check country and currency in DB
    if (countryfilter != "*") {
        dex::CountryInfo  countryinfo = dex::DexDB::self()->getCountryInfo(countryfilter);
        if (!countryinfo.enabled) {
            throw runtime_error("\nERROR: this country is disabled in DB\n");
        }
    }
    if (currencyfilter != "*") {
        dex::CurrencyInfo  currencyinfo = dex::DexDB::self()->getCurrencyInfo(currencyfilter);
        if (!currencyinfo.enabled) {
            throw runtime_error("\nERROR: this currency is disabled in DB\n");
        }
    }

    std::list<dex::MyOfferInfo> myoffers = dex::DexDB::self()->getMyOffers();
    for (auto i : myoffers) {
        if (typefilter == "buy"   && i.type != dex::Buy ) continue;
        if (typefilter == "sell"  && i.type != dex::Sell) continue;
        if (countryfilter  != "*" && countryfilter  != i.countryIso ) continue;
        if (currencyfilter != "*" && currencyfilter != i.currencyIso) continue;
        if (methodfilter != "*"   && methodfiltertype != i.paymentMethod) continue;
        if (statusfilter != "*"   && statusval        != i.status) continue;
        CDexOffer o(i.getOfferInfo(), i.type);
        UniValue v = o.getUniValue();
        v.push_back(Pair("status", i.status));
        v.push_back(Pair("statusStr", statuses[i.status]));
        result.push_back(v);
    }
    return result;
}




UniValue deldexoffer(const UniValue& params, bool fHelp)
{
    if (!fTxIndex) {
        throw runtime_error(
            "To use this feture please enable -txindex and make -reindex.\n"
        );
    }
    if (dex::DexDB::self() == 0) {
        throw runtime_error(
            "DexDB is not initialized.\n"
        );
    }
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "deldexoffer <hash>\n\n"
            "Delete offer from local DB and broadcast message.\n"
            "To do this, you need a private key in a wallet that matches the public key in the offer.\n"

            "\nArgument:\n"
            "\thash         (string) offer hash, hex digest.\n"

            "\nExample:\n"
            + HelpExampleCli("deldexoffer", "AABB...CCDD")
        );

    std::string strOfferHash = params[0].get_str();
    if (strOfferHash.empty()) {
        throw runtime_error("\nERROR: offer hash is empty");
    }

    uint256 hash = uint256S(strOfferHash);
    if (hash.IsNull()) {
        throw runtime_error("\nERROR: offer hash error\n");
    }

    //dex::DexDB db(strDexDbFile);

    CDexOffer offer;
    if (dex::DexDB::self()->isExistMyOfferByHash(hash)) {
        std::list<dex::MyOfferInfo> myoffers = dex::DexDB::self()->getMyOffers();
        for (auto i : myoffers) {
            if (i.hash == hash) offer = CDexOffer(i);
        }
    } else if (dex::DexDB::self()->isExistOfferBuyByHash(hash)) {
        offer = CDexOffer(dex::DexDB::self()->getOfferBuyByHash(hash), dex::Buy);
    } else if (dex::DexDB::self()->isExistOfferSellByHash(hash)) {
        offer = CDexOffer(dex::DexDB::self()->getOfferSellByHash(hash), dex::Sell);
    } else {
        throw runtime_error("\nERROR: offer not found in DB\n");
    }

    CDex dex(offer);
    std::string error;
    CKey key;
    if (!dex.FindKey(key, error)) {
        throw runtime_error(error.c_str());
    }

    std::vector<unsigned char> vchSign;
    if (!dex.SignOffer(key, vchSign, error)) {
        throw runtime_error(error.c_str());
    }

    if (offer.isMyOffer()) dex::DexDB::self()->deleteMyOffer  (offer.idTransaction);
    if (offer.isBuy())     dex::DexDB::self()->deleteOfferBuy (offer.idTransaction);
    if (offer.isSell())    dex::DexDB::self()->deleteOfferSell(offer.idTransaction);

    LOCK2(cs_main, cs_vNodes);
    BOOST_FOREACH(CNode* pNode, vNodes) {
        pNode->PushMessage(NetMsgType::DEXDELOFFER, offer, vchSign);
    }

    return NullUniValue;
}



UniValue adddexoffer(const UniValue& params, bool fHelp)
{
    if (!fTxIndex) {
        throw runtime_error(
            "To use this feture please enable -txindex and make -reindex.\n"
        );
    }
    if (dex::DexDB::self() == 0) {
        throw runtime_error(
            "DexDB is not initialized.\n"
        );
    }
    if (fHelp || params.size() != 5)
        throw runtime_error(
            "adddexoffer <type> <currency> <country> <price> <minamount>\n\n"
            "ONLY FOR TEST!!!!! Add dexoffer to DB and broadcast message.\n"

            "\nArgument:\n"
            "\ttype         (string) offer type, 'buy' or 'sell'\n"
            "\tcountry      (string) two-letter country code (ISO 3166-1 alpha-2 code)\n"
            "\tcurrency     (string) three-letter currency code (ISO 4217)\n"
            "\tprice        (number) offer price\n"
            "\tminamount    (number) offer minAmount\n"

            "\nExample:\n"
            + HelpExampleCli("adddexoffer", "sell RU RUB 100 100")
        );



    std::string strtype = params[0].get_str();

    if (strtype != "buy" && strtype != "sell") {
        throw runtime_error("\nERROR: offer type is empty");
    }

    dex::CountryIso  countryiso;
    if (!countryiso.isValid(params[1].get_str())) {
        throw runtime_error("\nERROR: invalid country code");
    }

    dex::CurrencyIso currencyiso;
    if (!currencyiso.isValid(params[2].get_str())) {
        throw runtime_error("\nERROR: invalid currency code");
    }

    int price = atoi(params[3].get_str().c_str());
    if (price <= 0) {
        throw runtime_error("\nERROR: invalid price");
    }
    int minAmount = atoi(params[4].get_str().c_str());
    if (minAmount <= 0) {
        throw runtime_error("\nERROR: invalid minAmount");
    }


    CDexOffer offer;
    CDexOffer::Type type = CDexOffer::BUY;
    if (strtype == "sell") type = CDexOffer::SELL;

    CKey key = pwalletMain->GeneratePrivKey();
    CPubKey pkey = key.GetPubKey();
    if (!pwalletMain->AddKeyPubKey(key, pkey)) {
        throw runtime_error("\nERROR: add key to wallet error");
    }


    std::string pubKey = HexStr(pkey.begin(), pkey.end());

    if (!offer.Create(type, pubKey, params[1].get_str(), params[2].get_str(), 1, price, minAmount, 30, "test offer", "test offer details")) {
        throw runtime_error("\nERROR: error create offer");
    }


    UniValue result(UniValue::VARR);
    dex::DexDB::self()->addMyOffer((dex::MyOfferInfo)(offer));

    return offer.getUniValue();
}



