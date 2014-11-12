/* Copyright 2013-2014 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __I2C_H
#define __I2C_H

struct i2c_request;

struct i2c_bus {
	struct list_node	link;
	struct dt_node		*dt_node;
	uint32_t		opal_id;
	int			(*queue_req)(struct i2c_request *req);
	struct i2c_request	*(*alloc_req)(struct i2c_bus *bus);
	void			(*free_req)(struct i2c_request *req);
};

struct i2c_request {
	struct list_node	link;
	struct i2c_bus		*bus;
	enum i2c_operation {
		I2C_READ,	/* RAW read from the device without offset */
		I2C_WRITE,	/* RAW write to the device without offset */
		SMBUS_READ,	/* SMBUS protocol read from the device */
		SMBUS_WRITE,	/* SMBUS protocol write to the device */
	} op;
	uint32_t		dev_addr;	/* Slave device address */
	uint32_t		offset_bytes;	/* Internal device offset */
	uint32_t		offset;		/* Internal device offset */
	uint32_t		rw_len;		/* Length of the data request */
	void			*rw_buf;	/* Data request buffer */
	void			(*completion)(	/* Completion callback */
					      int rc, struct i2c_request *req);
	void			*user_data;	/* Client data */
};

/* Generic i2c */
extern void i2c_add_bus(struct i2c_bus *bus);
extern struct i2c_bus *i2c_find_bus_by_id(uint32_t opal_id);

static inline struct i2c_request *i2c_alloc_req(struct i2c_bus *bus)
{
	return bus->alloc_req(bus);
}

static inline void i2c_free_req(struct i2c_request *req)
{
	req->bus->free_req(req);
}

static inline int i2c_queue_req(struct i2c_request *req)
{
	return req->bus->queue_req(req);
}

/* P8 implementation details */
extern void p8_i2c_init(void);
extern void p8_i2c_interrupt(uint32_t chip_id);

#endif /* __I2C_H */