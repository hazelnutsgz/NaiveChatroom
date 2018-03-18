

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <pthread.h>

#include "chatroom_utils.h"

#define MAX_CLIENTS 4

void initialize_server(connection_info *server_info, int port)
{
  if((server_info->socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    perror("Failed to create socket");
    exit(1);
  }

  server_info->address.sin_family = AF_INET;
  server_info->address.sin_addr.s_addr = INADDR_ANY;
  server_info->address.sin_port = htons(port);

  if(bind(server_info->socket, (struct sockaddr *)&server_info->address, sizeof(server_info->address)) < 0){
    perror("Binding failed");
    exit(1);
  }

  const int optVal = 1;
  const socklen_t optLen = sizeof(optVal);
  if(setsockopt(server_info->socket, SOL_SOCKET, SO_REUSEADDR, (void*) &optVal, optLen) < 0){
    perror("Set socket option failed");
    exit(1);
  }


  if(listen(server_info->socket, 3) < 0) {
    perror("Listen failed");
    exit(1);
  }

  //Accept and incoming connection
  printf("Waiting for incoming connections...\n");
}

//广播
void send_public_message(connection_info clients[], int sender, char *message_text){
  message msg;
  msg.type = PUBLIC_MESSAGE;
  strncpy(msg.username, clients[sender].username, 20);
  strncpy(msg.data, message_text, 256);
  int i = 0;
  for(i = 0; i < MAX_CLIENTS; i++){
    if(i != sender && clients[i].socket != 0){
      if(send(clients[i].socket, &msg, sizeof(msg), 0) < 0){
          perror("Send failed");
          exit(1);
      }
    }
  }
}

void send_private_message(connection_info clients[], int sender,
  char *username, char *message_text)
{
  message msg;
  msg.type = PRIVATE_MESSAGE;
  strncpy(msg.username, clients[sender].username, 20);
  strncpy(msg.data, message_text, 256);

  int i;
  for(i = 0; i < MAX_CLIENTS; i++)
  {
    if(i != sender && clients[i].socket != 0 && strcmp(clients[i].username, username) == 0){
      if(send(clients[i].socket, &msg, sizeof(msg), 0) < 0){
          perror("Send failed");
          exit(1);
      }
      return;
    }
  }

  msg.type = USERNAME_ERROR;
  sprintf(msg.data, "Username \"%s\" does not exist or is not logged in.", username);

  if(send(clients[sender].socket, &msg, sizeof(msg), 0) < 0){
      perror("Send failed");
      exit(1);
  }

}

//广播通知其他客户端某人下线的消息
void send_connect_message(connection_info *clients, int sender)
{
  message msg;
  msg.type = CONNECT;
  strncpy(msg.username, clients[sender].username, 21);
  int i = 0;
  for(i = 0; i < MAX_CLIENTS; i++)
  {
    if(clients[i].socket != 0)
    {
      if(i == sender)
      {
        msg.type = SUCCESS;
        if(send(clients[i].socket, &msg, sizeof(msg), 0) < 0)
        {
            perror("Send failed");
            exit(1);
        }
      }else
      {
        if(send(clients[i].socket, &msg, sizeof(msg), 0) < 0)
        {
            perror("Send failed");
            exit(1);
        }
      }
    }
  }
}

//广播通知其他客户端某个客户端下线的消息
void send_disconnect_message(connection_info *clients, char *username)
{
  message msg;
  msg.type = DISCONNECT;
  strncpy(msg.username, username, 21);
  int i = 0;
  for(i = 0; i < MAX_CLIENTS; i++){
    if(clients[i].socket != 0){
      if(send(clients[i].socket, &msg, sizeof(msg), 0) < 0){
          perror("Send failed");
          exit(1);
      }
    }
  }
}

void send_user_list(connection_info *clients, int receiver) {
  message msg;
  msg.type = GET_USERS;
  char *list = msg.data;

  int i;
  for(i = 0; i < MAX_CLIENTS; i++)
  {
    if(clients[i].socket != 0)
    {
      list = stpcpy(list, clients[i].username);
      list = stpcpy(list, "\n");
    }
  }

  if(send(clients[receiver].socket, &msg, sizeof(msg), 0) < 0){
      perror("Send failed");
      exit(1);
  }

}

void send_too_full_message(int socket)
{
  message too_full_message;
  too_full_message.type = TOO_FULL;

  if(send(socket, &too_full_message, sizeof(too_full_message), 0) < 0)
  {
      perror("Send failed");
      exit(1);
  }

  close(socket);
}

//close all the sockets before exiting
void stop_server(connection_info connection[]){
  int i;
  for(i = 0; i < MAX_CLIENTS; i++)
  {
    //send();
    close(connection[i].socket);
  }
  exit(0);
}


void handle_client_message(connection_info clients[], int sender)
{
  int read_size;
  message msg;


  if((read_size = recv(clients[sender].socket, &msg, sizeof(message), 0)) == 0){
    //如果发送空字节，表示切断连接
    printf("User disconnected: %s.\n", clients[sender].username);
    close(clients[sender].socket);

    //Rest the clients bit to zeros
    clients[sender].socket = 0;
    send_disconnect_message(clients, clients[sender].username);

  } else {

    switch(msg.type){

      //获取用户列表
      case GET_USERS:
        send_user_list(clients, sender);
      break;

      //登录
      case SET_USERNAME: ;
        int i;

        
        for(i = 0; i < MAX_CLIENTS; i++)
        {
          //查询clients中是否有重复的username，有的话就关闭连接,并且sender在服务端中client的位置赋值为0
          if(clients[i].socket != 0 && strcmp(clients[i].username, msg.username) == 0)
          {
            close(clients[sender].socket);
            clients[sender].socket = 0;
            return;
          }
        }

        strcpy(clients[sender].username, msg.username);
        printf("User connected: %s\n", clients[sender].username);
        send_connect_message(clients, sender);
      break;

      case PUBLIC_MESSAGE:
        send_public_message(clients, sender, msg.data);
      break;

      case PRIVATE_MESSAGE:
        send_private_message(clients, sender, msg.username, msg.data);
      break;

      default:
        fprintf(stderr, "Unknown message type received.\n");
      break;
    }
  }
}


//每次在轮询之前清零set数组，具体设置的方式为，先清零，然后将还没有关闭（socket非零）的clients中socket加入到set集合中去
int construct_fd_set(fd_set *set, connection_info *server_info, connection_info clients[]){
  FD_ZERO(set);
  FD_SET(STDIN_FILENO, set);
  FD_SET(server_info->socket, set);

  int max_fd = server_info->socket;
  int i;
  for(i = 0; i < MAX_CLIENTS; i++){
    if(clients[i].socket > 0){
      FD_SET(clients[i].socket, set);
      if(clients[i].socket > max_fd){
        max_fd = clients[i].socket;
      }
    }
  }
  return max_fd;
}

//处理新的连接,将接收到的new socket 赋值给clients数组的空闲位置，注意这里与之对应的是，如果检测到用户关闭连接，我们也需要把clients数组对应的位设置为0，来达到重新使用的目的
void handle_new_connection(connection_info *server_info, connection_info clients[]){
  int new_socket;
  int address_len;
  new_socket = accept(server_info->socket, (struct sockaddr*)&server_info->address, (socklen_t*)&address_len);

  if (new_socket < 0){
    perror("Accept Failed");
    exit(1);
  }

  int i;
  //Find the free slot in clients
  for(i = 0; i < MAX_CLIENTS; i++){

    if(clients[i].socket == 0) {
        clients[i].socket = new_socket;
        break;
    } 
      // if we can accept no more clients
    else if (i == MAX_CLIENTS -1){
      send_too_full_message(new_socket);
    }
  }
}

void handle_user_input(connection_info clients[]){
  char input[255];
  fgets(input, sizeof(input), stdin);
  trim_newline(input);

  if(input[0] == 'q') {
    stop_server(clients);
  }
}

int main(int argc, char *argv[]){
  puts("Starting server.");

  fd_set file_descriptors;

  connection_info server_info;
  connection_info clients[MAX_CLIENTS];

  int i;
  for(i = 0; i < MAX_CLIENTS; i++){
    clients[i].socket = 0;
  }

  if (argc != 2){
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(1);
  }

  //指定server的端口,信息放入server_info当中去
  initialize_server(&server_info, atoi(argv[1]));

  while(true){
    //每次大的循环的时候要记得把file_descriptor初始化
    int max_fd = construct_fd_set(&file_descriptors, &server_info, clients);

    if(select(max_fd+1, &file_descriptors, NULL, NULL, NULL) < 0){
      perror("Select Failed");
      stop_server(clients);
    }

    if(FD_ISSET(STDIN_FILENO, &file_descriptors)){
      handle_user_input(clients);
    }

    //如果在server的listenfd出现在file_descriptors里面，那么就建立新的连接（用accept生成connfd，然后加入到某个空余的client[i]上）
    if(FD_ISSET(server_info.socket, &file_descriptors)){
      //Wrapper of accept()
      handle_new_connection(&server_info, clients);
    }

    //遍历一遍client数组，看哪一个被set了，如果有的话，就处理这个message
    for(i = 0; i < MAX_CLIENTS; i++){
      if(clients[i].socket > 0 && FD_ISSET(clients[i].socket, &file_descriptors)){
        handle_client_message(clients, i);
      }
    }
  }

  return 0;
}
