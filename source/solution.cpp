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

#include <unistd.h>

struct Task {
    unsigned id_material;
    ACustomer customer;
    AOrderList orderList;

    Task(unsigned id_material, const ACustomer &customer, const AOrderList &orderList) : id_material(id_material),
                                                                                         customer(customer),
                                                                                         orderList(orderList) {}
};

struct PriceListWrapper {
    AProducer prod;
    APriceList priceList;
    bool end;
    bool knownSolution;

    PriceListWrapper(const AProducer &prod, const APriceList &priceList) : prod(prod), priceList(priceList),
                                                                           end(false), knownSolution(false) {}

    PriceListWrapper() : end(false), knownSolution(false) {}

    PriceListWrapper(unsigned material_id, bool knownSolution) : knownSolution(knownSolution) {
        priceList = make_shared<CPriceList>(material_id);
    }

    PriceListWrapper(bool end) : end(end) {}

};

struct BestPriceList {
    unsigned id_material;
    int pricingsLeft;
    set<AProducer> visited;
    map<unsigned, map<unsigned, double>> best;
    mutex lock;

    BestPriceList() = default;

    BestPriceList(unsigned id_material, int pricingsLeft) : id_material(id_material), pricingsLeft(pricingsLeft) {}

    void updateBestPriceList(const PriceListWrapper &priceListWrapper) {
        auto it = visited.find(priceListWrapper.prod);
        if (it == visited.end()) {
            visited.emplace_hint(it, priceListWrapper.prod);
            pricingsLeft--;

            for (const auto &e : priceListWrapper.priceList->m_List) {
                auto firstMap = best.find(e.m_W);
                if (firstMap == best.end()) {
                    best[e.m_W][e.m_H] = e.m_Cost;
                } else {
                    auto &secondMap = firstMap->second; // FUCKING AMPERSAND !!!!!!!
                    auto it = secondMap.find(e.m_H);
                    if (it == secondMap.end()) {
                        firstMap->second[e.m_H] = e.m_Cost;
                    } else {
                        if (it->second > e.m_Cost) {
                            it->second = e.m_Cost;
                        }
                    }
                }
            }
        }
    }

    APriceList createGetPriceList() {
        APriceList priceList = make_shared<CPriceList>(id_material);

        for (auto &i: best) {
            for (auto &j: i.second) {
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

    void print() {
        printf("#### Printing BestPriceList for material_id=%d, pricingsLeft=%d, visited.size()=%d ####\n", id_material,
               pricingsLeft, (int) visited.size());
        for (const auto &i : best)
            for (const auto &j : i.second) {
                printf("%d,%d,%lf\n", i.first, j.first, j.second);
            }
        printf("--------------\n");
    }
};

class CWeldingCompany {
private:
    atomic<int> activeCustomerCnt;

    map<int, vector<Task>> taskPool;
    deque<PriceListWrapper> priceListPool;
    map<int, BestPriceList> bestPriceList;
    set<int> obtainedOrders;
    set<int> knownSolutions;

    vector<AProducer> producers;
    vector<ACustomer> customers;

    vector<thread> threadsCustomer;
    vector<thread> threadsWorker;

    mutex mtx_taskPoolManip;
    mutex mtx_priceListPoolManip;
    mutex mtx_bestPriceListPoolManip;
    mutex mtx_obtainedOrdersManip;
    mutex mtx_knownSolutionsManip;

    condition_variable cv_priceListPoolEmpty; // protects from removing from empty pool
    condition_variable cv_taskPoolEmpty; // signals when TaskPool is empty

    //================================================================================
    void customerRoutine(ACustomer customer);

    void workerRoutine();

    PriceListWrapper popFromPricelist();

    void preprocessPricelist(PriceListWrapper &priceListWrapper);

    map<int, BestPriceList>::iterator findCreateBestPriceList(PriceListWrapper &priceListWrapper);

    void updateBestPriceList(PriceListWrapper &priceListWrapper, BestPriceList &best);

    void createUpdateTask(ACustomer customer, AOrderList orderList);

    void notifyAllProducersToSendPriceLists(unsigned int material_id);

    void customerThreadEndRoutine();

    void lastCustomerThreadRoutine();

    void taskCompleted(BestPriceList &best);

    void getOrdersFromCustomers(unsigned material_id);

    void addDummyPricelistForKnownSolution(unsigned material_id);

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

    while (true) {
        //printf("C: waiting for demand\n");
        orderList = customer->WaitForDemand();
        //printf("C: received demand\n");

        if (orderList == nullptr || orderList.get() == nullptr)
            break;

        //printf("C: creating or updating task\n");
        createUpdateTask(customer, orderList);
        //printf("C: creating or updating task done\n");

        //printf("C: getting orders from customers\n");
        getOrdersFromCustomers(orderList->m_MaterialID);
        //printf("C: getting orders from customers done\n");

        //printf("C: adding dummy price list for known solution\n");
        addDummyPricelistForKnownSolution(orderList->m_MaterialID);
        //printf("C: adding dummy price list for known solution done\n");
    }

    //printf("C: entering customerThreadEndRoutine\n");
    customerThreadEndRoutine();
    //printf("C: exiting from customerThreadEndRoutine\n");
}

void CWeldingCompany::getOrdersFromCustomers(unsigned material_id) {
    unique_lock<mutex> lock_obtainedOrders(mtx_obtainedOrdersManip);
    if (obtainedOrders.find(material_id) == obtainedOrders.end()) {
        obtainedOrders.insert(material_id);
        notifyAllProducersToSendPriceLists(material_id);
    }
    lock_obtainedOrders.unlock();
}

void CWeldingCompany::addDummyPricelistForKnownSolution(unsigned material_id) {
    unique_lock<mutex> lock_knownSolutions(mtx_knownSolutionsManip);

    if (knownSolutions.find(material_id) != knownSolutions.end()) {
        unique_lock<mutex> lock_priceListPoolManip(mtx_priceListPoolManip);
        priceListPool.emplace_back(PriceListWrapper(material_id, true));
        lock_priceListPoolManip.unlock();
    }

    lock_knownSolutions.unlock();
}

void CWeldingCompany::workerRoutine() {
    PriceListWrapper currPriceList;

    //printf("W: start\n");
    while (true) {
        //printf("W: popping from price list ...\n");
        currPriceList = popFromPricelist();
        //printf("W: popping from price list done\n");

        if (currPriceList.end)
            break;

        preprocessPricelist(currPriceList);

        //printf("W: find or create best pricelist\n");
        auto it_best = findCreateBestPriceList(currPriceList);
        //printf("W: find or create best pricelist done\n");

        if (currPriceList.knownSolution) {
            //printf("W: known solution\n");
            taskCompleted(it_best->second);
            //printf("W: known solution done\n");
        } else {
            //printf("W: unknown solution\n");
            updateBestPriceList(currPriceList, it_best->second);
            //printf("W: unknown solution done\n");
        }
    }
    //printf("W: end\n");
}

void CWeldingCompany::AddPriceList(AProducer prod, APriceList priceList) {
    unique_lock<mutex> ul(mtx_priceListPoolManip);
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
    //printf("===NEW RUN===\n");
    thrCount = 1;
    activeCustomerCnt = (int) customers.size();
    for (auto &c : customers)
        threadsCustomer.emplace_back([=] { customerRoutine(c); });

    for (unsigned i = 0; i < thrCount; i++)
        threadsWorker.emplace_back([=] { workerRoutine(); });
}

void CWeldingCompany::Stop() {
    for (auto &t : threadsCustomer)
        t.join();
    for (auto &t: threadsWorker)
        t.join();
    //printf("===END RUN===\n");
}

void CWeldingCompany::SeqSolve(APriceList priceList, COrder &order) {
    vector<COrder> vec;
    vec.emplace_back(order);
    ProgtestSolver(vec, std::move(priceList));
    order = vec.front();
}

PriceListWrapper CWeldingCompany::popFromPricelist() {
    PriceListWrapper poppedPriceList;
    unique_lock<mutex> lock_PriceListPool(mtx_priceListPoolManip);
    cv_priceListPoolEmpty.wait(lock_PriceListPool, [this]() { return !priceListPool.empty(); });
    poppedPriceList = priceListPool.front();
    priceListPool.pop_front();
    lock_PriceListPool.unlock();
    return poppedPriceList;
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
    return it;
}

void CWeldingCompany::updateBestPriceList(PriceListWrapper &priceListWrapper, BestPriceList &best) {
    // update BestPriceList with given PriceList

    unique_lock<mutex> lock_BestPriceListRecord(best.lock);

    best.updateBestPriceList(priceListWrapper);

    // all pricings collected, evaluate and return order to the customers
    if (best.pricingsLeft <= 0)
        taskCompleted(best);
}

void CWeldingCompany::taskCompleted(BestPriceList &best) {
    unique_lock<mutex> lock_TaskPool(mtx_taskPoolManip);
    auto taskGroup = taskPool.find(best.id_material);
    if (taskGroup != taskPool.end()) {
        for (auto &task : taskGroup->second) {
            auto it = best.createGetPriceList();
            ProgtestSolver(task.orderList->m_List, it);
            task.customer->Completed(task.orderList);
        }

        unique_lock<mutex> lock_knownSolutions(mtx_knownSolutionsManip);
        knownSolutions.insert(best.id_material);
        taskPool.erase(taskGroup);
    }

    if (taskPool.empty()) // signal that the TaskPool is empty
        cv_taskPoolEmpty.notify_one();
}

void CWeldingCompany::notifyAllProducersToSendPriceLists(unsigned int material_id) {
    for (const auto &p : producers) {
        p->SendPriceList(material_id);
    }
}

void CWeldingCompany::customerThreadEndRoutine() {
    activeCustomerCnt--;
    if (activeCustomerCnt == 0)
        lastCustomerThreadRoutine();
}

void CWeldingCompany::createUpdateTask(ACustomer customer, AOrderList orderList) {
    unique_lock<mutex> lock_TaskPool(mtx_taskPoolManip);
    auto it = taskPool.find(orderList->m_MaterialID);
    if (it == taskPool.end()) {
        taskPool.emplace_hint(it, orderList->m_MaterialID,
                              vector<Task>{Task(orderList->m_MaterialID, customer, orderList)});
    } else {
        it->second.emplace_back(Task(orderList->m_MaterialID, customer, orderList));
    }
    lock_TaskPool.unlock();
}

void CWeldingCompany::lastCustomerThreadRoutine() {
    //printf("C(last): Entered lastCustomerThreadRoutine\n");
    unique_lock<mutex> lock_TaskPool(mtx_taskPoolManip);
    //printf("C(last): waiting for taskPool to be empty, currently has %d elements\n", (int) taskPool.size());
    cv_taskPoolEmpty.wait(lock_TaskPool, [this]() { return taskPool.empty(); });
    unique_lock<mutex> lock_PriceListPool(mtx_priceListPoolManip);
    //printf("C: task pool is empty, inserting ending tokens\n");
    for (int i = 0; i < (int) threadsWorker.size(); i++) { // insert ending tokens for worker threads
        priceListPool.emplace_back(PriceListWrapper(true));
    }
    cv_priceListPoolEmpty.notify_all(); // wake up all sleeping workerThreads
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
