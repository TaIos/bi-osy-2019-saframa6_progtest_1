/*
 * UPSTREAM
 * HTTPS: https://gitlab.fit.cvut.cz/saframa6/bi-osy-2019-saframa6_progtest_1
 * SSH:   git@gitlab.fit.cvut.cz:saframa6/bi-osy-2019-saframa6_progtest_1.git
 */
#include <utility>

#ifndef __PROGTEST__

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <vector>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "progtest_solver.h"
#include "sample_tester.h"

using namespace std;
#endif /* __PROGTEST__ */

struct Task {
    unsigned id_material;
    ACustomer customer;
    AOrderList orderList;

    Task(unsigned id_material, const ACustomer &customer, const AOrderList &orderList) : id_material(id_material),
                                                                                         customer(customer),
                                                                                         orderList(orderList) {}
};

struct BestPriceList {
    unsigned id_material;
    int pricingsLeft;
    map<unsigned, map<unsigned, double>> best;
    mutex lock;

    BestPriceList() = default;

    BestPriceList(unsigned id_material, int pricingsLeft) : id_material(id_material), pricingsLeft(pricingsLeft) {}

    void decreasePricingsLeft() {
        pricingsLeft = pricingsLeft - 1;
    }

    void updateBestPriceList(const vector<CProd> &list) {
        for (const auto &e : list) {
            auto firstMap = best.find(e.m_W);
            if (firstMap == best.end()) {
                best[e.m_W][e.m_H] = e.m_Cost;
            } else {
                auto secondMap = firstMap->second;
                auto it = secondMap.find(e.m_H);
                if (it == secondMap.end())
                    firstMap->second[e.m_H] = e.m_Cost;
                else if (it->second > e.m_Cost)
                    it->second = e.m_Cost;
            }
        }
    }

    APriceList createGetPriceList() {
        APriceList priceList = make_shared<CPriceList>(id_material);

        for (auto i: best) {
            for (auto j: i.second) {
                priceList->Add(CProd(i.first, j.first, j.second));
            }
        }
        return priceList;
    }

    BestPriceList &operator=(const BestPriceList &other) {
        if (this != &other) {
            id_material = other.id_material;
            pricingsLeft = other.pricingsLeft;
            best = other.best;
        }
        return *this;
    }

    BestPriceList(const BestPriceList &other) {
        id_material = other.id_material;
        pricingsLeft = other.pricingsLeft;
        best = other.best;
    }
};

struct PriceListWrapper {
    AProducer prod;
    APriceList priceList;
    bool end;

    PriceListWrapper(const AProducer &prod, const APriceList &priceList) : prod(prod), priceList(priceList),
                                                                           end(false) {}

    PriceListWrapper() : end(false) {}

    PriceListWrapper(bool end) : end(end) {}
};

class CWeldingCompany {
private:
    int activeCustomerCnt;

    map<int, vector<Task>> taskPool;
    deque<PriceListWrapper> priceListPool;
    map<int, BestPriceList> bestPriceList;

    vector<AProducer> producers;
    vector<ACustomer> customers;

    vector<thread> threadsCustomer;
    vector<thread> threadsWorker;

    mutex mtx_taskPoolManip;
    mutex mtx_priceListPoolManip;
    mutex mtx_bestPriceListPoolManip;
    mutex mtx_customerActiveCntManip;

    condition_variable cv_priceListPoolEmpty; // protects from removing from empty pool
    condition_variable cv_taskPoolEmpty; // signals when TaskPool is empty

    //================================================================================
    void customerRoutine(ACustomer customer);

    void workerRoutine();

    PriceListWrapper popFromPricelist();

    void preprocessPricelist(PriceListWrapper &priceListWrapper);

    map<int, BestPriceList>::iterator findCreateBestPriceList(PriceListWrapper &priceListWrapper);

    void updateBestPriceList(const vector<CProd> &pricelist, BestPriceList &best);

    bool createUpdateTask(ACustomer customer, AOrderList orderList);

    void notifyAllProducersToSendPriceLists(unsigned int material_id);

    void customerThreadEndRoutine();

    void lastCustomerThreadRoutine();


public:

    void AddProducer(AProducer prod);

    void AddCustomer(ACustomer cust);

    void AddPriceList(AProducer prod, APriceList priceList);

    void Start(unsigned thrCount);

    void Stop();

    static void SeqSolve(APriceList priceList, COrder &order);
};

void CWeldingCompany::customerRoutine(ACustomer customer) {
    AOrderList orderList;
    bool created;

    printf("CUSTOMER thread START\n");
    while (true) {
        orderList = customer->WaitForDemand();

        if (orderList == nullptr || orderList.get() == nullptr)
            break;

        created = createUpdateTask(customer, orderList);

        if (created)
            notifyAllProducersToSendPriceLists(orderList->m_MaterialID);
    }
    customerThreadEndRoutine();
    printf("CUSTOMER thread END\n");
}

void CWeldingCompany::workerRoutine() {
    PriceListWrapper currPriceList;
    printf("WORKER thread START\n");

    while (true) {

        currPriceList = popFromPricelist();

        if (currPriceList.end)
            break;

        preprocessPricelist(currPriceList);

        auto it_best = findCreateBestPriceList(currPriceList);
        updateBestPriceList(currPriceList.priceList->m_List, it_best->second);
    }
    printf("WORKER thread END\n");
}

void CWeldingCompany::AddPriceList(AProducer prod, APriceList priceList) {
    unique_lock<mutex> ul(mtx_priceListPoolManip);
    printf("RECEIVED price list on material %d\n", priceList->m_MaterialID);
    priceListPool.emplace_back(PriceListWrapper(prod, priceList));
    cv_priceListPoolEmpty.notify_one();
    ul.unlock();
}

void CWeldingCompany::AddProducer(AProducer prod) {
    producers.emplace_back(prod);
}

void CWeldingCompany::AddCustomer(ACustomer cust) {
    customers.emplace_back(cust);
}

void CWeldingCompany::Start(unsigned thrCount) {
    activeCustomerCnt = (int) customers.size();
    for (auto &c : customers)
        threadsCustomer.emplace_back([=] { customerRoutine(c); });
    printf("CREATED %d customer threads\n", (int) threadsCustomer.size());
    for (unsigned i = 0; i < thrCount; i++)
        threadsWorker.emplace_back([=] { workerRoutine(); });
    printf("CREATED %d worker threads\n", thrCount);
    printf("START\n=================\n");
}

void CWeldingCompany::Stop() {
    printf("WAITING for customer threads to stop.\n");
    for (auto &t : threadsCustomer)
        t.join();
    printf("DONE\n");
    printf("WAITING for worker threads to stop.\n");
    for (auto &t: threadsWorker)
        t.join();
    printf("DONE\n");
}

void CWeldingCompany::SeqSolve(APriceList priceList, COrder &order) {
    vector<COrder> wrapper;
    wrapper.emplace_back(order);
    ProgtestSolver(wrapper, std::move(priceList));
}

PriceListWrapper CWeldingCompany::popFromPricelist() {
    PriceListWrapper poppedPriceList;
    unique_lock<mutex> lock_PriceListPool(mtx_priceListPoolManip);
    cv_priceListPoolEmpty.wait(lock_PriceListPool, [this]() { return !priceListPool.empty(); });
    poppedPriceList = priceListPool.front();
    priceListPool.pop_front();
    if (poppedPriceList.end)
        printf("REMOVED item[terminating] from price list\n");
    else
        printf("REMOVED item[non-terminating] from price list, material %d\n", poppedPriceList.priceList->m_MaterialID);
    lock_PriceListPool.unlock();
    return poppedPriceList;
}

void CWeldingCompany::preprocessPricelist(PriceListWrapper &priceListWrapper) {
// make CurrPriceList consistent (m_W <= m_H is always true)
    unsigned int tmp;
    for (auto &e : priceListWrapper.priceList->m_List) {
        if (e.m_W > e.m_H) {
            tmp = e.m_W;
            e.m_W = e.m_H;
            e.m_H = tmp;
        }
    }
}

map<int, BestPriceList>::iterator CWeldingCompany::findCreateBestPriceList(PriceListWrapper &priceListWrapper) {
    // Find BestPriceList for updating, construct it if record doesn't exist
    unsigned id = priceListWrapper.priceList->m_MaterialID;

    unique_lock<mutex> lock_BestPriceListPool(mtx_bestPriceListPoolManip);
    auto it = bestPriceList.find(id);
    if (it == bestPriceList.end()) {
        auto tmp = bestPriceList.emplace_hint(it, id, BestPriceList(id, (int) producers.size()));
        it = tmp;
    }
    lock_BestPriceListPool.unlock();
    return it;
}

void CWeldingCompany::updateBestPriceList(const vector<CProd> &pricelist, BestPriceList &best) {
    // update BestPriceList with given PriceList

    unique_lock<mutex> lock_BestPriceListRecord(best.lock);

    best.updateBestPriceList(pricelist);
    best.decreasePricingsLeft();

    // all pricings collected, evaluate and return order to the customers
    if (best.pricingsLeft == 0) {
        unique_lock<mutex> lock_TaskPool(mtx_taskPoolManip);
        auto taskGroup = taskPool.find(best.id_material);
        for (auto &task : taskGroup->second) {
            ProgtestSolver(task.orderList->m_List, best.createGetPriceList());
            task.customer->Completed(task.orderList);
        }
        printf("DONE task with material id %d\n", best.id_material);
        taskPool.erase(taskGroup);
        if (taskPool.empty()) // signal that the TaskPool is empty
            cv_taskPoolEmpty.notify_one();
        lock_TaskPool.unlock();
    }
    lock_BestPriceListRecord.unlock();
}

void CWeldingCompany::notifyAllProducersToSendPriceLists(unsigned int material_id) {
    for (const auto &p : producers) {
        p->SendPriceList(material_id);
    }
    printf("NOTIFIED all producers to send their pricelist on material %d\n", material_id);
}

void CWeldingCompany::customerThreadEndRoutine() {
    unique_lock<mutex> lock_CustomerActiveCnt(mtx_customerActiveCntManip);
    activeCustomerCnt = activeCustomerCnt - 1;
    if (activeCustomerCnt == 0)
        lastCustomerThreadRoutine();
    lock_CustomerActiveCnt.unlock();
}

bool CWeldingCompany::createUpdateTask(ACustomer customer, AOrderList orderList) {
    bool created = false;

    unique_lock<mutex> lock_TaskPool(mtx_taskPoolManip);
    auto it = taskPool.find(orderList->m_MaterialID);
    if (it == taskPool.end()) {
        taskPool.emplace_hint(it, orderList->m_MaterialID,
                              vector<Task>{Task(orderList->m_MaterialID, customer, orderList)});
        printf("CREATED new task for material %d\n", orderList->m_MaterialID);
        created = true;
    } else {
        printf("UPDATED task for material %d\n", orderList->m_MaterialID);
        it->second.emplace_back(Task(orderList->m_MaterialID, customer, orderList));
    }
    lock_TaskPool.unlock();

    return created;
}

void CWeldingCompany::lastCustomerThreadRoutine() {
    unique_lock<mutex> lock_TaskPool(mtx_taskPoolManip);
    cv_priceListPoolEmpty.wait(lock_TaskPool, [this]() { return priceListPool.empty(); });
    unique_lock<mutex> lock_PriceListPool(mtx_priceListPoolManip);
    for (int i = 0; i < (int) threadsWorker.size(); i++) { // insert ending tokens for worker threads
        priceListPool.emplace_back(PriceListWrapper(true));
    }
    lock_PriceListPool.unlock();
    lock_TaskPool.unlock();
    cv_priceListPoolEmpty.notify_all(); // wake up all sleeping workerThreads
    printf("LAST customer thread: waking all WORKER threads with ending tokens\n");
}

//-------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__

int main() {
    using namespace std::placeholders;
    CWeldingCompany test;

    AProducer p1 = make_shared<CProducerSync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
    AProducerAsync p2 = make_shared<CProducerAsync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
    test.AddProducer(p1);
    test.AddProducer(p2);
    test.AddCustomer(make_shared<CCustomerTest>(2));
    p2->Start();
    test.Start(3);
    test.Stop();
    p2->Stop();
    return 0;
}

#endif /* __PROGTEST__ */
