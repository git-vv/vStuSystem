#ifndef __I_RESOURCE_DAO_H__
#define __I_RESOURCE_DAO_H__

#include "resource_types.h"
#include <vector>

class IResourceDao {
public:
    virtual ~IResourceDao() {}

    virtual int InsertResource(const ResourceInfo& info) = 0;
    virtual int QueryResourceById(int32_t id, ResourceInfo& info) = 0;
    virtual int QueryAllResources(std::vector<ResourceInfo>& resources) = 0;
    virtual int UpdateResourceTotal(int32_t id, int32_t total_count) = 0;
    virtual int DeleteResource(int32_t id) = 0;
    virtual int CheckResourceInUse(int32_t resource_id, std::vector<std::string>& using_classes) = 0;
    virtual int InsertAllocation(const ResourceAllocation& alloc) = 0;
    virtual int QueryAllocationsByResourceId(int32_t resource_id, std::vector<ResourceAllocation>& allocs) = 0;
    virtual int QueryAllocationsByClassId(int32_t class_id, std::vector<ResourceAllocation>& allocs) = 0;
    virtual int QueryAllocationsByTimeRange(const std::string& start_time, const std::string& end_time, std::vector<ResourceAllocation>& allocs) = 0;
    virtual int CheckResourceCodeOccupied(int32_t resource_id, int32_t resource_code) = 0;
    virtual int CheckStudentResourceAllocated(int32_t resource_id, int32_t registration_id) = 0;
    virtual int IncrementResourceUsed(int32_t resource_id) = 0;
    virtual int DecrementResourceUsed(int32_t resource_id) = 0;
    virtual int QueryResourceByName(const std::string& name, ResourceInfo& info) = 0;
    virtual int QueryBedResourceRemain(int32_t resource_id) = 0;
    virtual int QueryResourceByType(int32_t resource_type, ResourceInfo& info) = 0;

    /**
     * @brief 原子资源分配：检查编号占用+插入分配+增加已用数量
     * @param alloc 分配记录
     * @return 0=成功, ERR_RESOURCE_CODE_OCCUPIED=编号已占用, 其他错误码
     */
    virtual int AllocateResourceAtomic(const ResourceAllocation& alloc) = 0;
};

#endif /* __I_RESOURCE_DAO_H__ */
