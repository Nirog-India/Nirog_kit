#ifndef _I2C_
#define _I2C_
#define i2c_port                  1

esp_err_t i2c_init();
int i2cdetect();
int i2c_read( uint8_t chip_addr, uint8_t data_addr, uint8_t *data_rd, size_t len);
int i2c_write_block(int chip_addr, int data_addr, uint8_t *wr_data, int len);
int i2c_write(int chip_addr, int data_addr, int wr_data);

#endif