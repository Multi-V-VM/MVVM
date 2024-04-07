# %%
import csv
import common_util
from multiprocessing import Pool
from matplotlib import pyplot as plt
import numpy as np
from common_util import parse_time, run_checkpoint_restore_outrage, get_avg_99percent
from collections import defaultdict
import pandas as pd
from datetime import datetime, timedelta
from statsmodels.tsa.arima.model import ARIMA
# %%

ip = ["128.114.53.32","128.114.59.237", "192.168.0.24", "192.168.0.23","128.114.59.234"]
port = 12347
port2 = 12346
new_port = 1235
new_port1 = 1238
new_port2 = 1236
new_port12 = 14440
cmd = [
    "cg",  # SLO for LU
]
folder = [
    "nas",
]
arg = [
    [],
    # [],
]
envs = [
    "OMP_NUM_THREADS=1",
]

pool = Pool(processes=1)

def get_cloud_result(data):
    df = pd.read_csv(data)

    # Convert the 'run_start_time' column to datetime format
    df['run_start_time'] = pd.to_datetime(df['run_start_time'])

    # Fill missing values in 'customers_out' with 0
    df['customers_out'].fillna(0, inplace=True)

    # Group the data by state and resample it to daily frequency
    state_data = df.groupby([pd.Grouper(key='run_start_time', freq='D'), 'state'])['customers_out'].sum().reset_index()

    # Function to train the ARIMA model and make predictions
    def predict_outages(state, start_date, end_date):
        # Filter the data for the specific state
        state_df = state_data[state_data['state'] == state]
        
        # Prepare the data for training
        train_data = state_df['customers_out'].values
        
        # Train the ARIMA model
        model = ARIMA(train_data, order=(1, 1, 1))
        model_fit = model.fit()
        
        # Generate future dates for prediction
        pred_dates = pd.date_range(start=start_date, end=end_date, freq='D')
        
        # Make predictions
        predictions = model_fit.forecast(steps=len(pred_dates))
        
        # Create a DataFrame with the predicted outages
        pred_df = pd.DataFrame({'date': pred_dates, 'predicted_outages': predictions})
        
        return pred_df

    # Example usage
    state = 'Washington'
    start_date = '2023-09-01'
    end_date = '2023-09-10'

    # Make predictions for the specified state and date range
    predictions = predict_outages(state, start_date, end_date)

    # Print the predicted outages
    print(f"Predicted Power Outages for {state}:")
    print(predictions)

    state = 'California'
    start_date = '2023-09-01'
    end_date = '2023-09-10'
    # Make predictions for the specified state and date range
    predictions = predict_outages(state, start_date, end_date)

    # Print the predicted outages
    print(f"Predicted Power Outages for {state}:")
    print(predictions)
    
    state = 'Connecticut'
    start_date = '2023-09-01'
    end_date = '2023-09-10'
    # Make predictions for the specified state and date range
    predictions = predict_outages(state, start_date, end_date)

    # Print the predicted outages
    print(f"Predicted Power Outages for {state}:")
    print(predictions)

# Convert the CSV data to a DataFrame
    df = pd.read_csv(data)

    # Convert the 'run_start_time' column to datetime format
    df['run_start_time'] = pd.to_datetime(df['run_start_time'])

    # Fill missing values in 'customers_out' with 0
    df['customers_out'].fillna(0, inplace=True)

    # Group the data by region and resample it to daily frequency
    region_data = df.groupby([pd.Grouper(key='run_start_time', freq='D'), 'state'])['customers_out'].sum().reset_index()

    # Pivot the data to have regions as columns
    region_data_pivoted = region_data.pivot(index='run_start_time', columns='state', values='customers_out')

    # Calculate the difference in customers out between regions
    region_data_pivoted['diff_1_2'] = region_data_pivoted['Alabama'] - region_data_pivoted['Alabama']
    region_data_pivoted['diff_2_3'] = region_data_pivoted['Alabama'] - region_data_pivoted['Alabama']
    region_data_pivoted['diff_3_1'] = region_data_pivoted['Alabama'] - region_data_pivoted['Alabama']

    # Find the maximum difference for each pair of regions
    max_diff_1_2 = region_data_pivoted['diff_1_2'].abs().max()
    max_diff_2_3 = region_data_pivoted['diff_2_3'].abs().max()
    max_diff_3_1 = region_data_pivoted['diff_3_1'].abs().max()

    # Find the corresponding times for the maximum differences
    time_1_2 = region_data_pivoted[region_data_pivoted['diff_1_2'].abs() == max_diff_1_2].index[0]
    time_2_3 = region_data_pivoted[region_data_pivoted['diff_2_3'].abs() == max_diff_2_3].index[0]
    time_3_1 = region_data_pivoted[region_data_pivoted['diff_3_1'].abs() == max_diff_3_1].index[0]

    # Print the results
    print(f"Biggest gap between Region 1 and Region 2: {max_diff_1_2} at time {time_1_2}")
    print(f"Biggest gap between Region 2 and Region 3: {max_diff_2_3} at time {time_2_3}")
    print(f"Biggest gap between Region 3 and Region 1: {max_diff_3_1} at time {time_3_1}")
    return time_1_2,time_2_3,time_3_1
    
def get_eneragy_outrage(time,time1,time2):
    results = []
    results1 = []
    aot = cmd[0] + ".aot"
    results1 = common_util.run_checkpoint_restore_outrage(
        aot,
        arg[0],
        envs[0],
        time,time1,time2,
        f"-o {ip[1]} -s {port}",
        f"-i {ip[1]} -e {port} -o {ip[2]} -s {new_port} -r",
        f"-i {ip[2]} -e {new_port} -o {ip[3]} -s {new_port1} -r",
        f"-i {ip[3]} -e {new_port1} -o {ip[0]} -s {port}",
    )
    return results1


def write_to_csv(filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]
    with open(filename, "a+", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(
            [
                "name",
                "fasttier",
                "slowtier",
                "snapshot Time",
            ]
        )

        # Write the data
        for idx, row in enumerate(fasttier):
            writer.writerow([row[0], row[1], slowtier[idx][1], snapshot[idx][1]])


def read_from_csv(filename):
    results = []
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        for idx, row in enumerate(reader):
            if idx == 0:
                continue
            results.append(row)
    return results


if __name__ == "__main__":
    # read_outrage_zip()
    # fasttier = get_cloud_result()
    # print("fasttier = ", fasttier)
    # slowtier = get_edge_result()
    # print("slowtier = ", slowtier)
    # snapshot = get_snapshot_overhead()
    # print("snapshot = ", snapshot)
    # # plot skew
    # write_to_csv("outrage_computing.csv")

    time,time1,time2 = get_cloud_result("eaglei_outages_2020.csv")
    print(time,time1,time2)
    reu = get_eneragy_outrage(10,30,40)
    with open("outrage.txt", "w") as f:
        f.write(str(reu))
    reu = ""
    with open("outrage.txt", "r") as f:
        reu = f.read()
    # with open("MVVM_checkpoint.ps.1.out") as f:
    #     checkpoint1 = f.read()
    # with open("MVVM_checkpoint.ps.out") as f:
    #     checkpoint = f.read()
    # with open("MVVM_restore.ps.6.out") as f:
    #     restore6 = f.read()
    # with open("MVVM_restore.ps.5.out") as f:
    #     restore5 = f.read()
    # with open("MVVM_restore.ps.4.out") as f:
    #     restore4 = f.read()
    # with open("MVVM_restore.ps.3.out") as f:
    #     restore3 = f.read()
    # with open("MVVM_restore.ps.2.out") as f:
    #     restore2 = f.read()
    # with open("MVVM_restore.ps.1.out") as f:
    #     restore1 = f.read()
    # with open("MVVM_restore.ps.out") as f:
    #     restore = f.read()

    # with open("MVVM_checkpoint.energy.1.out") as f:
    #     checkpoint_energy1 = f.read()
    # with open("MVVM_checkpoint.energy.out") as f:
    #     checkpoint_energy = f.read()
    # with open("MVVM_restore.energy.6.out") as f:
    #     restore_energy6 = f.read()
    # with open("MVVM_restore.energy.5.out") as f:
    #     restore_energy5 = f.read()
    # with open("MVVM_restore.energy.4.out") as f:
    #     restore_energy4 = f.read()
    # with open("MVVM_restore.energy.3.out") as f:
    #     restore_energy3 = f.read()
    # with open("MVVM_restore.energy.2.out") as f:
    #     restore_energy2 = f.read()
    # with open("MVVM_restore.energy.1.out") as f:
    #     restore_energy1 = f.read()
    # with open("MVVM_restore.energy.out") as f:
    #     restore_energy = f.read()

    plot_time(
        reu,
        None,
        None,
        None,
        None,
        # [
        #     checkpoint_energy,
        #     restore_energy,
        #     restore_energy1,
        #     restore_energy3,
        #     restore_energy2,
        # ],
        # [checkpoint1, restore3, restore4, restore5, restore6],
        # [
        #     checkpoint_energy1,
        #     restore_energy3,
        #     restore_energy4,
        #     restore_energy5,
        #     restore_energy6,
        # ],
        # [checkpoint1, restore3, restore4, restore5, restore6],
    )

# %%

# %%
