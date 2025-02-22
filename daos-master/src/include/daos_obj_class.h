/**
 * (C) Copyright 2015-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DAOS_OBJ_CLASS_H__
#define __DAOS_OBJ_CLASS_H__

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Predefined object classes
 * It describes schema of data distribution & protection.
 */
enum {
	OC_UNKNOWN	= 0,
	/**
	 * Object classes with no data protection
	 * NB: The first 50 IDs are reserved for backward compatibility
	 */
	OC_BACK_COMPAT	= 50,
	/** Single shard object */
	OC_TINY,
	/**
	 * Object with small number of shards.
	 * Number of shards of the class is chosen by DAOS based on the
	 * current size of the pool.
	 */
	OC_SMALL,
	/**
	 * Object with large number of shards.
	 * Number of shards of the class is chosen by DAOS based on the
	 * current size of the pool.
	 */
	OC_LARGE,
	/**
	 * Object with maximum number of shards.
	 * Number of shards of the class is chosen by DAOS based on the
	 * current size of the pool.
	 */
	OC_MAX,

	/**
	 * object classes protected by replication
	 */
	/**
	 * Tiny object protected by replication
	 * This object class has one redundancy group
	 */
	OC_RP_TINY	= 60,
	/**
	 * Replicated object with small number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_RP_SMALL,
	/**
	 * Replicated object with large number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_RP_LARGE,
	/**
	 * Replicated object with maximum number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_RP_MAX,

	/**
	 * Object classes protected by replication which supports Scalable
	 * Fetch (SF)
	 * SF classes have more replicas, so they are slower on update, but more
	 * scalable on fetch because they have more replicas to serve fetches.
	 */
	/**
	 * Tiny object protected by replication
	 * This object class has one redundancy group
	 */
	OC_RP_SF_TINY	= 70,
	/**
	 * (SF) Replicated object with small number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_RP_SF_SMALL,
	/**
	 * (SF) Replicated object with large number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_RP_SF_LARGE,
	/**
	 * (SF) Replicated object with maximum number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_RP_SF_MAX,

	/**
	 * Replicated object class which is extremely scalable for fetch.
	 * It has many replicas so it is very slow for update.
	 */
	OC_RP_XSF	= 80,

	/**
	 * Object classes protected by erasure code
	 */
	/**
	 * Tiny object protected by EC
	 * This object class has one redundancy group
	 */
	OC_EC_TINY	= 100,
	/**
	 * EC object with small number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_EC_SMALL,
	/**
	 * EC object with large number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_EC_LARGE,
	/**
	 * EC object with maximum number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_EC_MAX,

	/**
	 * Object classes with explicit layout
	 */
	/**
	 * Object classes with explicit layout but no data protection
	 * Examples:
	 * S1 : shards=1, S2 means shards=2, ...
	 * SX : spreading across all targets within the pool
	 */
	OC_S1		= 200,
	OC_S2,
	OC_S4,
	OC_S6,
	OC_S8,
	OC_S12,
	OC_S16,
	OC_S24,
	OC_S32,
	OC_S48,
	OC_S64,
	OC_S96,
	OC_S128,
	OC_S192,
	OC_S256,
	OC_S384,
	OC_S512,
	OC_S768,
	OC_S1K,
	OC_S2K,
	OC_S4K,
	OC_S6K,
	OC_S8K,
	OC_SX,

	/**
	 * Replicated object with explicit layout
	 * The first number is number of replicas, the number after G stands
	 * for number of redundancy Groups.
	 *
	 * Examples:
	 * 2G1 : 2 replicas group=1
	 * 3G2 : 3 replicas groups=2, ...
	 * 8GX : 8 replicas, it spreads across all targets within the pool
	 */
	/** 2-way replicated object classes */
	OC_RP_2G1	= 240,
	OC_RP_2G2,
	OC_RP_2G4,
	OC_RP_2G6,
	OC_RP_2G8,
	OC_RP_2G12,
	OC_RP_2G16,
	OC_RP_2G24,
	OC_RP_2G32,
	OC_RP_2G48,
	OC_RP_2G64,
	OC_RP_2G96,
	OC_RP_2G128,
	OC_RP_2G192,
	OC_RP_2G256,
	OC_RP_2G384,
	OC_RP_2G512,
	OC_RP_2G768,
	OC_RP_2G1K,
	OC_RP_2G2K,
	OC_RP_2G4K,
	OC_RP_2G6K,
	OC_RP_2G8K,
	OC_RP_2GX,
	OC_RP_2G3,

	/** 3-way replicated object classes */
	OC_RP_3G1	= 280,
	OC_RP_3G2,
	OC_RP_3G4,
	OC_RP_3G6,
	OC_RP_3G8,
	OC_RP_3G12,
	OC_RP_3G16,
	OC_RP_3G24,
	OC_RP_3G32,
	OC_RP_3G48,
	OC_RP_3G64,
	OC_RP_3G96,
	OC_RP_3G128,
	OC_RP_3G192,
	OC_RP_3G256,
	OC_RP_3G384,
	OC_RP_3G512,
	OC_RP_3G768,
	OC_RP_3G1K,
	OC_RP_3G2K,
	OC_RP_3G4K,
	OC_RP_3G6K,
	OC_RP_3G8K,
	OC_RP_3GX,

	/** 4-way replicated object classes */
	OC_RP_4G1	= 320,
	OC_RP_4G2,
	OC_RP_4G4,
	OC_RP_4G6,
	OC_RP_4G8,
	OC_RP_4G12,
	OC_RP_4G16,
	OC_RP_4G24,
	OC_RP_4G32,
	OC_RP_4G48,
	OC_RP_4G64,
	OC_RP_4G96,
	OC_RP_4G128,
	OC_RP_4G192,
	OC_RP_4G256,
	OC_RP_4G384,
	OC_RP_4G512,
	OC_RP_4G768,
	OC_RP_4G1K,
	OC_RP_4G2K,
	OC_RP_4G4K,
	OC_RP_4G6K,
	OC_RP_4G8K,
	OC_RP_4GX,

	/** 6-way replicated object classes */
	OC_RP_6G1	= 360,
	OC_RP_6G2,
	OC_RP_6G4,
	OC_RP_6G6,
	OC_RP_6G8,
	OC_RP_6G12,
	OC_RP_6G16,
	OC_RP_6G24,
	OC_RP_6G32,
	OC_RP_6G48,
	OC_RP_6G64,
	OC_RP_6G96,
	OC_RP_6G128,
	OC_RP_6G192,
	OC_RP_6G256,
	OC_RP_6G384,
	OC_RP_6G512,
	OC_RP_6G768,
	OC_RP_6G1K,
	OC_RP_6G2K,
	OC_RP_6G4K,
	OC_RP_6G6K,
	OC_RP_6G8K,
	OC_RP_6GX,

	/** 8-way replicated object classes */
	OC_RP_8G1	= 400,
	OC_RP_8G2,
	OC_RP_8G4,
	OC_RP_8G6,
	OC_RP_8G8,
	OC_RP_8G12,
	OC_RP_8G16,
	OC_RP_8G24,
	OC_RP_8G32,
	OC_RP_8G48,
	OC_RP_8G64,
	OC_RP_8G96,
	OC_RP_8G128,
	OC_RP_8G192,
	OC_RP_8G256,
	OC_RP_8G384,
	OC_RP_8G512,
	OC_RP_8G768,
	OC_RP_8G1K,
	OC_RP_8G2K,
	OC_RP_8G4K,
	OC_RP_8G6K,
	OC_RP_8G8K,
	OC_RP_8GX,

	/* OC_XSF will map to one of these */
	OC_RP_12G1	= 440,
	OC_RP_16G1,
	OC_RP_24G1,
	OC_RP_32G1,
	OC_RP_48G1,
	OC_RP_64G1,
	OC_RP_128G1,
	/**
	 * Erasure coded object with explicit layout
	 * - the first number is data cells number within a redundancy group
	 * - the number after P is parity cells number within a redundancy group
	 * - the number after G is number of redundancy Groups.
	 *
	 * Examples:
	 * - 2P1G1: 2+1 EC object with one redundancy group
	 * - 4P2G8: 4+2 EC object with 8 redundancy groups
	 * - 8P2G2: 8+2 EC object with 2 redundancy groups
	 * - 16P2GX: 16+2 EC object spreads across all targets within the pool
	 */
	/** EC 2+1 object classes */
	OC_EC_2P1G1	= 500,
	OC_EC_2P1G2,
	OC_EC_2P1G4,
	OC_EC_2P1G6,
	OC_EC_2P1G8,
	OC_EC_2P1G12,
	OC_EC_2P1G16,
	OC_EC_2P1G24,
	OC_EC_2P1G32,
	OC_EC_2P1G48,
	OC_EC_2P1G64,
	OC_EC_2P1G96,
	OC_EC_2P1G128,
	OC_EC_2P1G192,
	OC_EC_2P1G256,
	OC_EC_2P1G384,
	OC_EC_2P1G512,
	OC_EC_2P1G768,
	OC_EC_2P1G1K,
	OC_EC_2P1G2K,
	OC_EC_2P1G4K,
	OC_EC_2P1G6K,
	OC_EC_2P1G8K,
	OC_EC_2P1GX,

	/** EC 2+2 object classes */
	OC_EC_2P2G1	= 540,
	OC_EC_2P2G2,
	OC_EC_2P2G4,
	OC_EC_2P2G6,
	OC_EC_2P2G8,
	OC_EC_2P2G12,
	OC_EC_2P2G16,
	OC_EC_2P2G24,
	OC_EC_2P2G32,
	OC_EC_2P2G48,
	OC_EC_2P2G64,
	OC_EC_2P2G96,
	OC_EC_2P2G128,
	OC_EC_2P2G192,
	OC_EC_2P2G256,
	OC_EC_2P2G384,
	OC_EC_2P2G512,
	OC_EC_2P2G768,
	OC_EC_2P2G1K,
	OC_EC_2P2G2K,
	OC_EC_2P2G4K,
	OC_EC_2P2G6K,
	OC_EC_2P2G8K,
	OC_EC_2P2GX,

	/** EC 4+1 object classes */
	OC_EC_4P1G1	= 580,
	OC_EC_4P1G2,
	OC_EC_4P1G4,
	OC_EC_4P1G6,
	OC_EC_4P1G8,
	OC_EC_4P1G12,
	OC_EC_4P1G16,
	OC_EC_4P1G24,
	OC_EC_4P1G32,
	OC_EC_4P1G48,
	OC_EC_4P1G64,
	OC_EC_4P1G96,
	OC_EC_4P1G128,
	OC_EC_4P1G192,
	OC_EC_4P1G256,
	OC_EC_4P1G384,
	OC_EC_4P1G512,
	OC_EC_4P1G768,
	OC_EC_4P1G1K,
	OC_EC_4P1G2K,
	OC_EC_4P1G4K,
	OC_EC_4P1G6K,
	OC_EC_4P1G8K,
	OC_EC_4P1GX,

	/** EC 4+2 object classes */
	OC_EC_4P2G1	= 620,
	OC_EC_4P2G2,
	OC_EC_4P2G4,
	OC_EC_4P2G6,
	OC_EC_4P2G8,
	OC_EC_4P2G12,
	OC_EC_4P2G16,
	OC_EC_4P2G24,
	OC_EC_4P2G32,
	OC_EC_4P2G48,
	OC_EC_4P2G64,
	OC_EC_4P2G96,
	OC_EC_4P2G128,
	OC_EC_4P2G192,
	OC_EC_4P2G256,
	OC_EC_4P2G384,
	OC_EC_4P2G512,
	OC_EC_4P2G768,
	OC_EC_4P2G1K,
	OC_EC_4P2G2K,
	OC_EC_4P2G4K,
	OC_EC_4P2G6K,
	OC_EC_4P2G8K,
	OC_EC_4P2GX,

	/** EC 8+1 object classes */
	OC_EC_8P1G1	= 660,
	OC_EC_8P1G2,
	OC_EC_8P1G4,
	OC_EC_8P1G6,
	OC_EC_8P1G8,
	OC_EC_8P1G12,
	OC_EC_8P1G16,
	OC_EC_8P1G24,
	OC_EC_8P1G32,
	OC_EC_8P1G48,
	OC_EC_8P1G64,
	OC_EC_8P1G96,
	OC_EC_8P1G128,
	OC_EC_8P1G192,
	OC_EC_8P1G256,
	OC_EC_8P1G384,
	OC_EC_8P1G512,
	OC_EC_8P1G768,
	OC_EC_8P1G1K,
	OC_EC_8P1G2K,
	OC_EC_8P1G4K,
	OC_EC_8P1G6K,
	OC_EC_8P1G8K,
	OC_EC_8P1GX,

	/** EC 8+2 object classes */
	OC_EC_8P2G1	= 700,
	OC_EC_8P2G2,
	OC_EC_8P2G4,
	OC_EC_8P2G6,
	OC_EC_8P2G8,
	OC_EC_8P2G12,
	OC_EC_8P2G16,
	OC_EC_8P2G24,
	OC_EC_8P2G32,
	OC_EC_8P2G48,
	OC_EC_8P2G64,
	OC_EC_8P2G96,
	OC_EC_8P2G128,
	OC_EC_8P2G192,
	OC_EC_8P2G256,
	OC_EC_8P2G384,
	OC_EC_8P2G512,
	OC_EC_8P2G768,
	OC_EC_8P2G1K,
	OC_EC_8P2G2K,
	OC_EC_8P2G4K,
	OC_EC_8P2G6K,
	OC_EC_8P2G8K,
	OC_EC_8P2GX,

	/** EC 16+1 object classes */
	OC_EC_16P1G1	= 740,
	OC_EC_16P1G2,
	OC_EC_16P1G4,
	OC_EC_16P1G6,
	OC_EC_16P1G8,
	OC_EC_16P1G12,
	OC_EC_16P1G16,
	OC_EC_16P1G24,
	OC_EC_16P1G32,
	OC_EC_16P1G48,
	OC_EC_16P1G64,
	OC_EC_16P1G96,
	OC_EC_16P1G128,
	OC_EC_16P1G192,
	OC_EC_16P1G256,
	OC_EC_16P1G384,
	OC_EC_16P1G512,
	OC_EC_16P1G768,
	OC_EC_16P1G1K,
	OC_EC_16P1G2K,
	OC_EC_16P1G4K,
	OC_EC_16P1G6K,
	OC_EC_16P1G8K,
	OC_EC_16P1GX,

	/** EC 16+2 object classes */
	OC_EC_16P2G1	= 780,
	OC_EC_16P2G2,
	OC_EC_16P2G4,
	OC_EC_16P2G6,
	OC_EC_16P2G8,
	OC_EC_16P2G12,
	OC_EC_16P2G16,
	OC_EC_16P2G24,
	OC_EC_16P2G32,
	OC_EC_16P2G48,
	OC_EC_16P2G64,
	OC_EC_16P2G96,
	OC_EC_16P2G128,
	OC_EC_16P2G192,
	OC_EC_16P2G256,
	OC_EC_16P2G384,
	OC_EC_16P2G512,
	OC_EC_16P2G768,
	OC_EC_16P2G1K,
	OC_EC_16P2G2K,
	OC_EC_16P2G4K,
	OC_EC_16P2G6K,
	OC_EC_16P2G8K,
	OC_EC_16P2GX,

	/** Class ID equal or higher than this is reserved */
	OC_RESERVED		= (1U << 10),
};

enum daos_obj_schema {
	DAOS_OS_SINGLE,			/**< Single stripe object */
	DAOS_OS_STRIPED,		/**< Fix striped object */
	DAOS_OS_DYN_STRIPED,	/**< Dynamically striped object */
	DAOS_OS_DYN_CHUNKED,	/**< Dynamically chunked object */
};

enum daos_obj_resil {
	DAOS_RES_EC,            /**< Erasure code */
	DAOS_RES_REPL,          /**< Replication */
};

/** Object class attributes */
struct daos_oclass_attr {
	/** reserved: object placement schema, used by placement algorithm */
	enum daos_obj_schema	 ca_schema;
	/** Resilience method, replication or erasure code */
	enum daos_obj_resil		 ca_resil;			// 副本/纠删码
	/** reserved */
	unsigned int			 ca_resil_degree;
	/** Initial # redundancy group, unnecessary for some schemas */
	unsigned int			 ca_grp_nr;			// group的数量, 每个obj都可以指定group的数量
	union {										// 类似于ceph中的pg数，dkey通过哈希group的数量均匀映射到group上
		/** Replication attributes */
		struct daos_rp_attr {
			/** Protocol of replicating, reserved */
			unsigned int	 r_proto;
			/** Number of replicas */
			unsigned int	 r_num;				// 副本的个数
		} rp;

		/** Erasure coding attributes */
		struct daos_ec_attr {
			/** number of data cells (k) */
			unsigned short	 e_k;				// 数据片的个数
			/** number of parity cells (p) */
			unsigned short	 e_p;				// 校验片的个数
			/** length of each block of data (cell) */
			unsigned int	 e_len;				// 默认都是1M的大小
		} ec;
	} u;
	/** TODO: add more attributes */
};

/** object class ID */
typedef uint16_t		daos_oclass_id_t;
/** object class hints */
typedef uint16_t		daos_oclass_hints_t;
/** object feature bits */
typedef uint16_t		daos_ofeat_t;

/** List of object classes, used for class enumeration */
struct daos_oclass_list {
	/** List length, actual buffer size */
	uint32_t		 cl_nr;
	/** List length, returned buffer size */
	uint32_t		 cl_nr_out;
	/** Actual list of class IDs */
	daos_oclass_id_t	*cl_cids;
	/** Attributes of each listed class, optional */
	struct daos_oclass_attr	*cl_cattrs;
};

/**
 * Return the Object class ID given the object class name in string format.
 *
 * \param[in]	name	Object class name.
 *
 * \return		The Object class ID, 0 / OC_UNKNOWN if unknown.
 */
int
daos_oclass_name2id(const char *name);

/**
 * Return the list of object class.
 *
 * \param[in]	size	length in bytes of str buffer.
 * \param[out]	str	buffer to get all registered oclass names
 *
 * \return		>= 0 on success and required length of str, -1 if error.
 */
size_t
daos_oclass_names_list(size_t size, char *str);

/**
 *
 * Return total number of object classes
 *
 * \param[in]	opts	reserved options
 *
 * \return		> 0 Number object classes
 */
int
daos_oclass_nr(int opts);

/**
 * Return the object class name given it's ID.
 *
 * \param[in]	oc_id	Object class ID.
 * \param[out]	name	buffer for the name of the object class to be copied
 *			into it.
 *
 * \return		0 on success, -1 if invalid class.
 */
int
daos_oclass_id2name(daos_oclass_id_t oc_id, char *name);

/**
 * Register a new object class in addition to the default ones (see DAOS_OC_*).
 * An object class cannot be unregistered for the time being.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	cid	ID for the new object class.
 * \param[in]	attr	Attributes for the new object class.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EXIST	Object class ID already existed
 */
int
daos_obj_register_class(daos_handle_t coh, daos_oclass_id_t cid,
			struct daos_oclass_attr *attr, daos_event_t *ev);

/**
 * Query attributes of an object class by its ID.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	cid	Class ID to query.
 * \param[out]	attr	Returned attributes of the object class.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	Nonexistent class ID
 */
int
daos_obj_query_class(daos_handle_t coh, daos_oclass_id_t cid,
		     struct daos_oclass_attr *attr, daos_event_t *ev);

/**
 * List existing object classes.
 *
 * \param[in]	coh	Container open handle.
 * \param[out]	list	Sink buffer for returned class list.
 * \param[in,out]
 *		anchor	Hash anchor for the next call. It should be set to
 *			zeroes for the first call. It should not be altered
 *			by caller between calls.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_obj_list_class(daos_handle_t coh, struct daos_oclass_list *list,
		    daos_anchor_t *anchor, daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_OBJ_CLASS_H__ */
